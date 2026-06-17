// ADS1263 on RX72N – example using SCI1 in simple-SPI mode
//
// Pin assignment (from BSP-generated r_sci_rx_pinset.c / Pin.c)
// ──────────────────────────────────────────────────────────────────
//  RX72N pin  │ ADS1263 pin │ Function
// ────────────┼─────────────┼────────────────────────────────────────
//  P26        │ DIN         │ SMOSI1  (SCI1 MOSI)       [FIT-managed]
//  P27        │ SCLK        │ SCK1    (SCI1 clock)       [FIT-managed]
//  P30        │ DOUT        │ SMISO1  (SCI1 MISO)        [FIT-managed]
//  P31        │ /CS         │ GPIO output, active-low    [manual]
//  P04        │ /DRDY       │ IRQ12 input, active-low    [direct ICU]
// ──────────────────────────────────────────────────────────────────
//
// IRQ12 is on P04 for the 176-pin LFBGA package (check your hardware
//   manual if you have a different package).  The ISEL bit in P04PFS
//   enables the ICU IRQ12 input; no PSEL is needed.
//
// SPI parameters
//   Mode    : 1  (CPOL=0, CPHA=1 – ADS1263 samples DIN on falling SCLK)
//   Bit rate: 1 MHz  (ADS1263 max 10 MHz; conservative start value)
//   Bit order: MSB first
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {
#include "platform.h"           // Renesas BSP – pulls in iodefine.h and r_bsp.h
#include "r_sci_rx_if.h"
#include "r_sci_rx_pinset.h"
#include "r_bsp_common.h"
}

#include "ads1263.hpp"
#include "log.h"

// ─── SCI callback (defined in ads1263.cpp) ────────────────────────────────────
extern "C" void ads1263_sci_callback(void* p_args);

// ─── IRQ12 ISR (ADS1263 /DRDY) ───────────────────────────────────────────────
// In C++ the STATIC variant avoids a linkage-spec conflict:
//   R_BSP_PRAGMA_STATIC_INTERRUPT → static void f(void) __attribute__((interrupt(...), used));
//   R_BSP_ATTRIB_STATIC_INTERRUPT → static
// Both are equivalent to the non-STATIC forms for GNURX vector placement.

R_BSP_PRAGMA_STATIC_INTERRUPT(irq12_isr, VECT(ICU, IRQ12))

static volatile bool s_drdy = false;   // set by ISR, cleared by read()

R_BSP_ATTRIB_STATIC_INTERRUPT void irq12_isr(void)
{
    s_drdy = true;
}

// Configure the ICU for IRQ12 on P44 (falling edge, /DRDY active-low).
// P44 is the chosen IRQ12-capable pin on the RX72M 176-pin LFBGA package.
// Uses only iodefine.h macros – no r_irq_rx FIT module required.
static void irq12_init(void)
{
    // ── MPC: enable IRQ12 input function on P44 ──────────────────────────────
    // Only the ISEL bit is set; PSEL stays 0 (IRQ is an ICU function, not SFR).
    R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_MPC);
    MPC.P44PFS.BIT.ISEL = 1;
    R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_MPC);

    // ── GPIO direction: input ─────────────────────────────────────────────────
    PORT4.PDR.BIT.B4 = 0;

    // ── ICU: configure falling-edge trigger ──────────────────────────────────
    // IRQMD: 00=low-level, 01=falling, 10=rising, 11=both
    ICU.IRQCR[12].BIT.IRQMD = 0x1;     // falling edge

    // ── ICU: clear any pending flag, set priority, enable ────────────────────
    IR(ICU, IRQ12)  = 0;
    IPR(ICU, IRQ12) = 3;                // priority 3 – same as SCI1 group (SCI_CFG_ERI_TEI_PRIORITY)
    IEN(ICU, IRQ12) = 1;
}

// ─── CS pin setup ─────────────────────────────────────────────────────────────

static void setup_cs_pin(void)
{
    PORT3.PODR.BIT.B1 = 1;     // CS deasserted (high) before enabling output
    PORT3.PDR.BIT.B1  = 1;     // P31 output
}

// ─── RESET/PWDN pin setup (P72 → ADS1263 pin 20, active-low) ──────────────────
// Drive high (deasserted) before enabling output so the ADC leaves reset/power-
// down and responds on the bus.  The driver pulses this pin during begin().
static void setup_reset_pin(void)
{
    PORT7.PODR.BIT.B2 = 1;     // /RESET deasserted (high) before enabling output
    PORT7.PDR.BIT.B2  = 1;     // P72 output
}

// ─── SCI1 SPI open ────────────────────────────────────────────────────────────

// Keep the handle in a static so a debugger soft-reset (which skips _INITSCT)
// can close the previously opened channel before re-opening it.
static sci_hdl_t s_sci1_hdl = nullptr;

static sci_hdl_t open_sci1_spi(void)
{
    // ── Pin setup (BSP-generated, skips P31 which is manual GPIO /CS) ────────
    R_SCI_PinSet_SCI1();

    // Simple-SPI (SSPI) mode: unlike SCI_MODE_SYNC (fixed phase, SCLK idle-high),
    // SSPI lets us select SPI Mode 1 (CPOL=0/SCLK idle-low, CPHA=1) for the ADS1263.
    sci_cfg_t cfg;
    cfg.sspi.spi_mode     = SCI_SPI_MODE_1;   // CPOL=0, CPHA=1
    cfg.sspi.bit_rate     = 1000000UL;        // 1 MHz
    cfg.sspi.msb_first    = true;
    cfg.sspi.invert_data  = false;
    cfg.sspi.int_priority = 3;                // txi/tei/rxi IPL (1..15); 0 or >15 → INVALID_ARG

    // ── Close stale handle from a debugger soft-reset (warm-reset defence) ───
    if (s_sci1_hdl != nullptr)
    {
        R_SCI_Close(s_sci1_hdl);
        s_sci1_hdl = nullptr;
    }

    sci_err_t err = R_SCI_Open(SCI_CH1,
                                SCI_MODE_SSPI,
                                &cfg,
                                ads1263_sci_callback,
                                &s_sci1_hdl);

    if (err == SCI_ERR_CH_NOT_CLOSED)
    {
        // Startup ran but FIT state wasn't cleared (edge case); try once more.
        R_SCI_Close(s_sci1_hdl);
        err = R_SCI_Open(SCI_CH1, SCI_MODE_SSPI, &cfg, ads1263_sci_callback, &s_sci1_hdl);
    }

    if (err != SCI_SUCCESS)
    {
        while (true) { /* check SCI_CFG_CH1_INCLUDED=1 and SCI_CFG_SSPI_INCLUDED=1 */ }
    }

    // ── Activate SMOSI1 peripheral mode AFTER SCI TE is set by R_SCI_Open ───
    // Pin.c defers this step with the comment "set PMR after TE=1".
    PORT2.PMR.BIT.B6 = 1U;

    return s_sci1_hdl;
}

// ─── Entry point ──────────────────────────────────────────────────────────────

int main(void)
{
    // ── Logging must come first: SCI7 is independent of SCI1/IRQ12 ──────────
    log_init();
    log_puts("\r\n=== ADS1263 RX72M driver starting ===\r\n");

    setup_cs_pin();
    log_puts("CS pin P31 ready\r\n");

    setup_reset_pin();         // P72 high → bring ADS1263 out of reset/power-down
    log_puts("RESET pin P72 deasserted (high)\r\n");

    irq12_init();
    log_puts("IRQ12 /DRDY configured (P04, falling edge, priority 3)\r\n");

    sci_hdl_t sci_hdl = open_sci1_spi();
    log_puts("SCI1 SPI Mode-1 @ 1 MHz ready\r\n");

    // PORT3.PODR.BYTE = output data register for Port 3; 0x02 = bit 1 (P31)
    ADS1263 adc(sci_hdl, PORT3.PODR.BYTE, 0x02U);

    // Hardware RESET/PWDN on P72 (PORT7 bit 2). begin() pulses this pin to reset
    // the ADC instead of using the software RESET command.
    adc.setResetPin(&PORT7.PODR.BYTE, 0x04U);

    // ── Diagnostic: raw ID read tells us what kind of failure we have ────────
    // ADS1263 ID register (00h) reads 0x2x (DEV_ID=001 in bits 7:5).
    //   0x2x  → SPI link OK
    //   0x00  → MISO stuck low: no clock, device in reset/PWDN, or DOUT not wired
    //   0xFF  → MISO stuck high / floating (DOUT not driven / not connected)
    //   other → SPI mode or bit-order mismatch
    const uint8_t raw_id = adc.readReg(ADS1263Reg::ID);
    log_printf("ADS1263 ID register = 0x%02X (expect 0x2x)\r\n", (unsigned)raw_id);

    if (!adc.begin(ADS1263Rate::SPS_100, ADS1263Gain::GAIN_1))
    {
        log_printf("ADS1263 init FAILED (ID=0x%02X) — see checklist below\r\n",
                   (unsigned)raw_id);
        log_puts("  - RESET/PWDN (pin 20) wired to P72 and driven high\r\n");
        log_puts("  - XTAL1/CLKIN tied to DGND (internal osc), XTAL2 floating\r\n");
        log_puts("  - AVDD=5V, DVDD=3.3V, REFOUT & BYPASS 1uF caps fitted\r\n");
        log_puts("  - Check DIN/DOUT/SCLK/CS wiring (DOUT->P30, DIN->P26)\r\n");
        while (true) {}
    }
    log_puts("ADS1263 ID OK, configured 100 SPS / Gain 1\r\n");

    // Use STATUS-byte polling for conversion-ready (does NOT depend on the
    // /DRDY pin/IRQ being wired).  read() issues RDATA1 and checks the STATUS
    // byte's ADC1 "new data" bit over SPI.
    //
    // If you have /DRDY (ADS1263 pin 14) physically wired to P44, you can switch
    // to the lower-latency IRQ path instead:  adc.setDRDYFlag(&s_drdy);
    adc.setDRDYFlag(nullptr);
    (void)s_drdy;   // IRQ flag unused in polling mode

    adc.start();
    log_puts("Continuous conversions started\r\n");

    // ── Diagnostic register dump ────────────────────────────────────────────
    // If these read back as expected, SPI + config are good and conversions
    // should flag ready.  If they read 0x00 or 0xFF, SPI is not actually
    // exchanging data (wiring / SPI mode), regardless of the earlier ID match.
    //   Expect: IFACE=0x04, MODE0=0x00, MODE1=0x00, MODE2=0x07 (100SPS/gain1),
    //           POWER=0x01, REFMUX=0x00, ID=0x2x
    log_printf("REG  IFACE=0x%02X MODE0=0x%02X MODE1=0x%02X MODE2=0x%02X\r\n",
               (unsigned)adc.readReg(ADS1263Reg::IFACE),
               (unsigned)adc.readReg(ADS1263Reg::MODE0),
               (unsigned)adc.readReg(ADS1263Reg::MODE1),
               (unsigned)adc.readReg(ADS1263Reg::MODE2));
    log_printf("REG  POWER=0x%02X REFMUX=0x%02X INPMUX=0x%02X ID=0x%02X\r\n",
               (unsigned)adc.readReg(ADS1263Reg::POWER),
               (unsigned)adc.readReg(ADS1263Reg::REFMUX),
               (unsigned)adc.readReg(ADS1263Reg::INPMUX),
               (unsigned)adc.readReg(ADS1263Reg::ID));

    log_puts("Scanning AIN0..AIN9 single-ended vs AINCOM, VREF=2.5V int\r\n");

    // Channels to scan: AIN0..AIN9, each measured single-ended against AINCOM.
    // (AINCOM is the shared negative input — pin 3.)  Sinc1 filter means the
    // first conversion after each mux change is already settled.
    static const ADS1263Mux k_channels[] = {
        ADS1263Mux::AIN0, ADS1263Mux::AIN1, ADS1263Mux::AIN2, ADS1263Mux::AIN3,
        ADS1263Mux::AIN4, ADS1263Mux::AIN5, ADS1263Mux::AIN6, ADS1263Mux::AIN7,
        ADS1263Mux::AIN8, ADS1263Mux::AIN9,
    };
    static const uint8_t k_num_channels =
        static_cast<uint8_t>(sizeof(k_channels) / sizeof(k_channels[0]));

    uint32_t sweep     = 0U;
    uint32_t err_count = 0U;

    while (true)
    {
        ++sweep;
        log_printf("--- sweep %u ---\r\n", (unsigned)sweep);

        for (uint8_t ch = 0U; ch < k_num_channels; ++ch)
        {
            // Switch the positive input to AINx, negative fixed to AINCOM.
            // Writing INPMUX restarts the ADC1 conversion automatically.
            if (!adc.setMux(k_channels[ch], ADS1263Mux::AINCOM))
            {
                ++err_count;
                log_printf("AIN%u: setMux FAILED\r\n", (unsigned)ch);
                continue;
            }

            int32_t raw = 0;
            if (adc.read(raw, 500U))
            {
                const float voltage =
                    ADS1263::toVolts(raw, 2.5f, ADS1263Gain::GAIN_1);
                log_printf("  AIN%u: raw=%d  v=%+.6f V\r\n",
                           (unsigned)ch, (int)raw, (double)voltage);
            }
            else
            {
                ++err_count;
                // STATUS byte from the last poll tells us why it timed out:
                //   0x00 → no STATUS byte / no response (conversions not running
                //          or STATUS not enabled); bit6 never set
                //   0x40 → would have been "ready" (shouldn't reach here)
                //   0xFF → MISO stuck high
                log_printf("  AIN%u: read timeout (STATUS=0x%02X err=%u)\r\n",
                           (unsigned)ch, (unsigned)adc.lastStatus(),
                           (unsigned)err_count);
            }
        }

        // pace the sweeps so the UART log stays readable
        R_BSP_SoftwareDelay(500U, BSP_DELAY_MILLISECS);
    }
    return 0;   /* unreachable — satisfies C++ ::main return type */
}

// ─── Advanced usage (not compiled) ───────────────────────────────────────────
//
// Revert to STATUS-byte polling (no /DRDY pin wired):
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
