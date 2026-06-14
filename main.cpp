// ADS1263 on RX72N – example using SCI6 in simple-SPI mode
//
// Pin assignment
// ─────────────────────────────────────────────────────────────
//  RX72N pin  │ ADS1263 pin │ Function
// ────────────┼─────────────┼─────────────────────────────────
//  P26        │ DOUT        │ SMISO6  (SCI6 MISO)  [FIT-managed]
//  P27        │ DIN         │ SMOSI6  (SCI6 MOSI)  [FIT-managed]
//  P30        │ SCLK        │ SCK6    (SCI6 clock)  [FIT-managed]
//  P31        │ /CS         │ GPIO output, active-low [manual]
// ─────────────────────────────────────────────────────────────
//
// NOTE: The SCI FIT module configures the MPC registers for P26/P27/P30
//       via the auto-generated r_sci_rx_pinset.c (Smart Configurator).
//       Verify that the pinset file targets SCI channel 6 and those exact
//       pins for your RX72N package / board variant.
//
// SPI parameters
//   Mode    : 1  (CPOL=0, CPHA=1 – ADS1263 samples DIN on falling SCLK edge)
//   Bit rate: 1 MHz  (ADS1263 max is 10 MHz; start conservatively)
//   Bit order: MSB first
//
// Reference voltage: internal 2.5 V (VREF pin left open or tied to AVSS)
// Input measured   : AIN0(+) vs AIN1(-)
// ─────────────────────────────────────────────────────────────────────────────

#include "platform.h"           // Renesas BSP – pulls in iodefine.h and r_bsp.h
#include "r_sci_rx_if.h"
#include "r_bsp_common.h"

#include "ads1263.hpp"

// ─── SCI callback (defined in ads1263.cpp, declared here) ─────────────────────
extern "C" void ads1263_sci_callback(void* p_args);

// ─── Setup helpers ────────────────────────────────────────────────────────────

// Configure P31 as a push-pull output (CS line).
// P26/P27/P30 are left to R_SCI_Open() / the FIT pinset function.
static void setup_cs_pin(void)
{
    // 1. Disable analog input on P31 (clear ASEL bit in MPC if needed)
    //    For a pure digital port pin no MPC change is required.

    // 2. Set direction: output
    PORT3.PDR.BIT.B1 = 1;

    // 3. Drive high (CS deasserted) before the driver object is created
    PORT3.PODR.BIT.B1 = 1;
}

// Open SCI6 as a SPI master.
// Returns the channel handle; halts on configuration error.
static sci_hdl_t open_sci6_spi(void)
{
    sci_cfg_t cfg;
    cfg.sync.spi_mode    = SCI_SPI_MODE_1;   // CPOL=0, CPHA=1
    cfg.sync.bit_rate    = 1000000UL;         // 1 MHz
    cfg.sync.msb_first   = true;
    cfg.sync.invert_data = false;

    sci_hdl_t hdl;
    const sci_err_t err = R_SCI_Open(SCI_CH6,
                                      SCI_MODE_SYNC,
                                      &cfg,
                                      ads1263_sci_callback,
                                      &hdl);

    // In production code handle each error case; here we halt on any failure.
    if (err != SCI_SUCCESS)
    {
        while (true) { /* configuration error – check FIT pinset and clock tree */ }
    }

    return hdl;
}

// ─── Entry point ──────────────────────────────────────────────────────────────

void main(void)
{
    // --- Hardware initialisation -------------------------------------------

    setup_cs_pin();

    sci_hdl_t sci_hdl = open_sci6_spi();

    // Create driver; PORT3.PODR.BYTE is the output data register for Port 3,
    // 0x02 selects bit 1 (P31).
    ADS1263 adc(sci_hdl, PORT3.PODR.BYTE, 0x02U);

    // --- ADS1263 initialisation --------------------------------------------
    //   • 100 SPS data rate
    //   • PGA bypassed (gain = 1×)
    //   • Differential: AIN0(+) vs AIN1(-)
    //   • Internal 2.5 V reference (default after reset)

    if (!adc.begin(ADS1263Rate::SPS_100, ADS1263Gain::GAIN_1))
    {
        // ID verification failed – check wiring and SPI timing
        while (true) {}
    }

    // Start continuous conversions on ADC1
    adc.start();

    // --- Main measurement loop ---------------------------------------------

    while (true)
    {
        int32_t raw = 0;

        if (adc.read(raw, 500U))     // 500 ms timeout
        {
            const float voltage = ADS1263::toVolts(raw, 2.5f, ADS1263Gain::GAIN_1);

            // Replace with your application's output (UART printf, display, etc.)
            (void)voltage;
            (void)raw;

            // Example: send over SCI UART
            //   char buf[64];
            //   snprintf(buf, sizeof(buf), "raw=%ld  V=%.6f\r\n", raw, (double)voltage);
            //   R_SCI_Send(uart_hdl, (uint8_t*)buf, strlen(buf));
        }
        else
        {
            // Timeout – ADC not converting or wiring issue
        }

        // Delay between reads (optional; remove for maximum throughput)
        R_BSP_SoftwareDelay(10U, BSP_DELAY_MILLISECS);  // ~10 ms between samples
    }
}

// ─── Advanced usage examples (not compiled) ──────────────────────────────────
//
// Single-ended measurement vs AINCOM (AIN2 as input):
//   adc.setMux(ADS1263Mux::AIN2, ADS1263Mux::AINCOM);
//
// Differential AIN4(+) vs AIN5(-) at 32× gain, 20 SPS:
//   adc.setMux(ADS1263Mux::AIN4, ADS1263Mux::AIN5);
//   adc.setGain(ADS1263Gain::GAIN_32);
//   adc.setRate(ADS1263Rate::SPS_20);
//   float v = ADS1263::toVolts(raw, 2.5f, ADS1263Gain::GAIN_32);
//
// Direct register write (e.g. configure TDACP excitation current):
//   adc.writeReg(ADS1263Reg::IDACMAG, 0x07);  // 1 mA IDAC
//   adc.writeReg(ADS1263Reg::IDACMUX, 0x00);  // route IDAC1 to AIN0
