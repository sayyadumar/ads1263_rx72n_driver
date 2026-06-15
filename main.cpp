// ADS1263 on RX72N – example using SCI1 in simple-SPI mode
//
// Pin assignment
// ──────────────────────────────────────────────────────────────────
//  RX72N pin  │ ADS1263 pin │ Function
// ────────────┼─────────────┼────────────────────────────────────────
//  P26        │ DOUT        │ SMISO1  (SCI1 MISO)       [FIT-managed]
//  P27        │ DIN         │ SMOSI1  (SCI1 MOSI)       [FIT-managed]
//  P30        │ SCLK        │ SCK1    (SCI1 clock)      [FIT-managed]
//  P31        │ /CS         │ GPIO output, active-low   [manual]
//  <IRQ12 pin>│ /DRDY       │ IRQ12 input, active-low   [manual + r_irq_rx]
// ──────────────────────────────────────────────────────────────────
//
// IRQ12 pin: check your RX72N hardware manual section 21 (ICU interrupt
//   source table) for which physical pin carries IRQ12 on your package.
//   Common assignments are P04 (100-pin) or PE4 (176-pin).  The ISEL
//   bit in that pin's PFS register enables the IRQ input function.
//
// r_irq_rx_config.h prerequisite:
//   #define IRQ_CFG_CH12_INCLUDED   (1)
//
// SPI parameters
//   Mode    : 1  (CPOL=0, CPHA=1 – ADS1263 samples DIN on falling SCLK)
//   Bit rate: 1 MHz  (ADS1263 max 10 MHz; conservative start value)
//   Bit order: MSB first
// ─────────────────────────────────────────────────────────────────────────────

#include "platform.h"           // Renesas BSP – pulls in iodefine.h and r_bsp.h
#include "r_sci_rx_if.h"
#include "r_irq_rx_if.h"
#include "r_bsp_common.h"

#include "ads1263.hpp"

// ─── SCI callback (defined in ads1263.cpp) ────────────────────────────────────
extern "C" void ads1263_sci_callback(void* p_args);

// ─── IRQ12 (ADS1263 /DRDY) ───────────────────────────────────────────────────

static irq_hdl_t     s_irq12_hdl;
static volatile bool s_drdy = false;   // set by ISR, cleared by read()

static void irq12_callback(void* /*p_args*/)
{
    s_drdy = true;
}

// Configure and open IRQ12 for the ADS1263 /DRDY signal.
//
// Three things must be correct or you get IRQ_ERR_NOT_CLOSED:
//   1. r_irq_rx_config.h  → IRQ_CFG_CH12_INCLUDED = 1
//   2. MPC ISEL bit       → enable IRQ input on the chosen pin
//   3. Close before open  → clear stale handle after warm reset
//
// ── Step 1: verify r_irq_rx_config.h ─────────────────────────────────────────
//   Open r_irq_rx_config.h in your FIT module and confirm:
//     #define IRQ_CFG_CH12_INCLUDED   (1)
//   Without this, the channel is compiled out and R_IRQ_Open(12,...) returns
//   IRQ_ERR_BAD_CHAN, which the FIT plumbing can surface as IRQ_ERR_NOT_CLOSED
//   from a previous call that left the channel in a bad state.
//
// ── Step 2: MPC pin configuration ────────────────────────────────────────────
//   Find the pin that carries IRQ12 in your RX72N package pin function table
//   (hardware manual, section "Port Function Select").  Set ISEL = 1 in its
//   PxxPFS register; PSEL stays 0 (IRQ is not a peripheral function, it is an
//   ICU function enabled via the ISEL bit only).
//
// ── Step 3: close-before-open ────────────────────────────────────────────────
//   After a watchdog / debugger reset, r_irq_rx keeps its channel-open flag
//   set in RAM.  R_IRQ_Open() then sees the channel as still open and returns
//   IRQ_ERR_NOT_CLOSED.  The fix is to call R_IRQ_Close() first; it is a
//   harmless no-op on the very first cold boot.

static void irq12_init(void)
{
    // ── MPC: enable IRQ12 on the chosen pin ──────────────────────────────────
    // EXAMPLE uses P04 (100-pin package).  Replace with your actual pin.
    // Only the ISEL bit is touched; PSEL remains 0.
    MPC.PWPR.BIT.B0WI  = 0;     // unlock step 1
    MPC.PWPR.BIT.PFSWE = 1;     // unlock step 2
    MPC.P04PFS.BIT.ISEL = 1;    // enable IRQ12 input on P04  ← verify your pin
    MPC.PWPR.BIT.PFSWE = 0;     // lock step 2
    MPC.PWPR.BIT.B0WI  = 1;     // lock step 1

    // ── GPIO direction: input ─────────────────────────────────────────────────
    PORT0.PDR.BIT.B4 = 0;       // input  ← match port/bit to your IRQ12 pin

    // ── Close any stale handle (prevents IRQ_ERR_NOT_CLOSED after warm reset) ─
    R_IRQ_Close(s_irq12_hdl);   // safe even if channel was never opened

    // ── Configure and open ────────────────────────────────────────────────────
    irq_cfg_t cfg;
    cfg.trigger    = IRQ_TRIG_FALLING;        // /DRDY asserts low on conversion done
    cfg.filter     = IRQ_FILTER_PCLKB_DIV_8;  // light digital glitch filter
    cfg.priority   = 3;                        // must be ≥ SCI1 TXI/RXI priority
    cfg.p_callback = irq12_callback;

    const irq_err_t err = R_IRQ_Open(12U, &cfg, &s_irq12_hdl);
    if (err != IRQ_SUCCESS)
    {
        // Possible causes:
        //   IRQ_ERR_NOT_CLOSED – channel still locked; ensure R_IRQ_Close() above ran
        //   IRQ_ERR_BAD_CHAN   – IRQ_CFG_CH12_INCLUDED is 0 in r_irq_rx_config.h
        while (true) {}
    }

    R_IRQ_Enable(s_irq12_hdl);
}

// ─── CS pin setup ─────────────────────────────────────────────────────────────

static void setup_cs_pin(void)
{
    PORT3.PDR.BIT.B1  = 1;     // P31 output
    PORT3.PODR.BIT.B1 = 1;     // CS deasserted (high)
}

// ─── SCI1 SPI open ────────────────────────────────────────────────────────────

static sci_hdl_t open_sci1_spi(void)
{
    sci_cfg_t cfg;
    cfg.sync.spi_mode    = SCI_SPI_MODE_1;
    cfg.sync.bit_rate    = 1000000UL;
    cfg.sync.msb_first   = true;
    cfg.sync.invert_data = false;

    // Close-before-open: same warm-reset defence as IRQ12 above
    sci_hdl_t stale = (sci_hdl_t)0;
    R_SCI_Close(stale);

    sci_hdl_t hdl;
    sci_err_t err = R_SCI_Open(SCI_CH1,
                                SCI_MODE_SYNC,
                                &cfg,
                                ads1263_sci_callback,
                                &hdl);

    if (err == SCI_ERR_LOCK)
    {
        R_SCI_Close(hdl);
        err = R_SCI_Open(SCI_CH1, SCI_MODE_SYNC, &cfg, ads1263_sci_callback, &hdl);
    }

    if (err != SCI_SUCCESS)
    {
        while (true) { /* check SCI_CFG_CH1_INCLUDED and FIT pinset */ }
    }

    return hdl;
}

// ─── Entry point ──────────────────────────────────────────────────────────────

void main(void)
{
    setup_cs_pin();
    irq12_init();

    sci_hdl_t sci_hdl = open_sci1_spi();

    // PORT3.PODR.BYTE = output data register for Port 3; 0x02 = bit 1 (P31)
    ADS1263 adc(sci_hdl, PORT3.PODR.BYTE, 0x02U);

    if (!adc.begin(ADS1263Rate::SPS_100, ADS1263Gain::GAIN_1))
    {
        while (true) {}     // ID check failed – verify SPI wiring and timing
    }

    // Hand the DRDY flag to the driver so read() uses the IRQ path
    adc.setDRDYFlag(&s_drdy);

    adc.start();

    while (true)
    {
        int32_t raw = 0;

        if (adc.read(raw, 500U))
        {
            const float voltage = ADS1263::toVolts(raw, 2.5f, ADS1263Gain::GAIN_1);
            (void)voltage;
            (void)raw;
            // e.g. R_SCI_Send(uart_hdl, buf, len);
        }
    }
}

// ─── Advanced usage (not compiled) ───────────────────────────────────────────
//
// Revert to STATUS-byte polling (no DRDY pin wired):
//   adc.setDRDYFlag(nullptr);
//
// Single-ended vs AINCOM:
//   adc.setMux(ADS1263Mux::AIN2, ADS1263Mux::AINCOM);
//
// 32× gain differential:
//   adc.setMux(ADS1263Mux::AIN4, ADS1263Mux::AIN5);
//   adc.setGain(ADS1263Gain::GAIN_32);
//   adc.setRate(ADS1263Rate::SPS_20);
//   float v = ADS1263::toVolts(raw, 2.5f, ADS1263Gain::GAIN_32);
