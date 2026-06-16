#pragma once

#include <cstdint>
#include "r_sci_rx_if.h"

// Minimal SPI driver for the ADS1263, built on the Renesas SCI FIT module
// in simple synchronous (SPI) mode. Only what is needed to reset the part
// and read/write registers — no gain/mux/rate configuration, no DRDY/IRQ.
//
// ADS1263 requires SPI Mode 1 (CPOL=0, CPHA=1): DIN is sampled by the part
// on the falling edge of SCLK.

class Ads1263Spi
{
public:
    // hdl     – SCI handle already opened with R_SCI_Open(..., SCI_MODE_SYNC, ...)
    //           using SCI_SPI_MODE_1, MSB first.
    // cs_podr – reference to the CS pin's port output data register byte.
    // cs_mask – bit mask of the CS pin within that byte.
    Ads1263Spi(sci_hdl_t hdl, volatile uint8_t& cs_podr, uint8_t cs_mask);

    // Send the RESET command (0x06). Caller is responsible for any
    // power-up delay before calling this.
    bool reset();

    // Write a single register. addr is the raw register address (0x00-0x1A).
    bool writeReg(uint8_t addr, uint8_t val);

    // Read a single register. addr is the raw register address (0x00-0x1A).
    // On success, val holds the register contents and the function returns true.
    bool readReg(uint8_t addr, uint8_t& val);

private:
    sci_hdl_t         m_hdl;
    volatile uint8_t& m_cs_podr;
    uint8_t           m_cs_mask;

    void csLow();
    void csHigh();

    // Full-duplex SPI exchange of 'len' bytes. tx or rx may be nullptr to
    // use an internal scratch buffer (send zeros / discard received bytes).
    bool transfer(const uint8_t* tx, uint8_t* rx, uint16_t len);
};
