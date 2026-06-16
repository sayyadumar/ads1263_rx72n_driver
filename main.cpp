// Minimal ADS1263 bring-up test on RX72N
//
// Goal: verify SPI communication by reading the ADS1263 ID register and
// printing the result over a UART console. No gain/mux/rate setup, no
// DRDY/IRQ — just enough to confirm the SPI link is alive.
//
// Channel assignment (placeholder — update once the Smart Configurator
// project files are provided):
//   SCI1 → UART console (TXD1/RXD1)
//   SCI6 → SPI link to ADS1263 (SMISO6/SMOSI6/SCK6), CS via GPIO
//
// SPI parameters: Mode 1 (CPOL=0, CPHA=1), MSB first.
// Bit rate is intentionally low (100 kHz) for first bring-up; raise it
// once basic communication is confirmed.
//
// TODO once pin/peripheral assignment is confirmed from the .scfg / FIT
// configuration files:
//   - Replace the CS port/bit placeholder in setup_cs_pin() / main()
//   - Confirm SCI1 and SCI6 are in fact the correct channels
//   - Confirm the FIT pinset (r_sci_rx_pinset.c) routes the right pins
// ─────────────────────────────────────────────────────────────────────────────

#include "platform.h"       // Renesas BSP – iodefine.h, r_bsp.h
#include "r_sci_rx_if.h"
#include "r_bsp_common.h"

#include "ads1263_spi.hpp"
#include "uart_console.hpp"

// Callbacks defined in ads1263_spi.cpp / uart_console.cpp
extern "C" void ads1263_spi_sci_callback(void* p_args);
extern "C" void uart_console_sci_callback(void* p_args);

namespace
{
    constexpr uint8_t ADS1263_REG_ID = 0x00U;
}

// ─── CS pin (placeholder) ─────────────────────────────────────────────────────
// TODO: replace PORT3 / bit 1 with the actual CS pin once known.
static void setup_cs_pin(void)
{
    PORT3.PDR.BIT.B1  = 1;     // output
    PORT3.PODR.BIT.B1 = 1;     // CS deasserted (idle high)
}

// ─── SCI1: UART console ───────────────────────────────────────────────────────

static sci_hdl_t open_uart_sci1(void)
{
    sci_cfg_t cfg;
    cfg.async.baud_rate    = 115200UL;
    cfg.async.clk_src      = SCI_CLK_INT;
    cfg.async.data_size    = SCI_DATA_8BIT;
    cfg.async.parity_en    = SCI_PARITY_OFF;
    cfg.async.parity_type  = SCI_EVEN_PARITY;
    cfg.async.stop_bits    = SCI_STOPBITS_1;
    cfg.async.int_priority = 3;

    sci_hdl_t hdl;
    sci_err_t err = R_SCI_Open(SCI_CH1,
                                SCI_MODE_ASYNC,
                                &cfg,
                                uart_console_sci_callback,
                                &hdl);

    if (err == SCI_ERR_LOCK)
    {
        R_SCI_Close(hdl);
        err = R_SCI_Open(SCI_CH1, SCI_MODE_ASYNC, &cfg, uart_console_sci_callback, &hdl);
    }

    if (err != SCI_SUCCESS)
    {
        while (true) { /* check SCI_CFG_CH1_INCLUDED and FIT pinset */ }
    }

    return hdl;
}

// ─── SCI6: SPI link to ADS1263 ────────────────────────────────────────────────

static sci_hdl_t open_spi_sci6(void)
{
    sci_cfg_t cfg;
    cfg.sync.spi_mode    = SCI_SPI_MODE_1;   // CPOL=0, CPHA=1
    cfg.sync.bit_rate    = 100000UL;          // 100 kHz – conservative for bring-up
    cfg.sync.msb_first   = true;
    cfg.sync.invert_data = false;

    // Close-before-open guards against a stale lock after a warm/debugger reset
    // (otherwise R_SCI_Open can return SCI_ERR_LOCK / IRQ_ERR_NOT_CLOSED).
    sci_hdl_t stale = (sci_hdl_t)0;
    R_SCI_Close(stale);

    sci_hdl_t hdl;
    sci_err_t err = R_SCI_Open(SCI_CH6,
                                SCI_MODE_SYNC,
                                &cfg,
                                ads1263_spi_sci_callback,
                                &hdl);

    if (err == SCI_ERR_LOCK)
    {
        R_SCI_Close(hdl);
        err = R_SCI_Open(SCI_CH6, SCI_MODE_SYNC, &cfg, ads1263_spi_sci_callback, &hdl);
    }

    if (err != SCI_SUCCESS)
    {
        while (true) { /* check SCI_CFG_CH6_INCLUDED and FIT pinset */ }
    }

    return hdl;
}

// ─── Entry point ──────────────────────────────────────────────────────────────

void main(void)
{
    setup_cs_pin();

    sci_hdl_t uart_hdl = open_uart_sci1();
    UartConsole console(uart_hdl);

    console.print("ADS1263 ID test\r\n");

    sci_hdl_t spi_hdl = open_spi_sci6();

    // PORT3.PODR.BYTE = output data register for Port 3; 0x02 = bit 1.
    // TODO: replace with the real CS port/bit.
    Ads1263Spi adc(spi_hdl, PORT3.PODR.BYTE, 0x02U);

    R_BSP_SoftwareDelay(10U, BSP_DELAY_MILLISECS);   // power-on settle

    if (!adc.reset())
    {
        console.print("SPI transfer failed on RESET command\r\n");
    }
    R_BSP_SoftwareDelay(2U, BSP_DELAY_MILLISECS);     // tREGACQ

    uint8_t id = 0U;
    if (adc.readReg(ADS1263_REG_ID, id))
    {
        console.print("ID register = ");
        console.printHex8(id);
        console.print("\r\n");

        // Upper 3 bits should read 0b001 for a genuine ADS1263
        if ((id >> 5U) == 0x01U)
        {
            console.print("Device ID matches ADS1263\r\n");
        }
        else
        {
            console.print("Unexpected ID value - check wiring/SPI mode\r\n");
        }
    }
    else
    {
        console.print("SPI transfer failed on ID read\r\n");
    }

    while (true) {}
}
