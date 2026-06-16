#include "ads1263_spi.hpp"
#include "r_bsp_common.h"   // R_BSP_SoftwareDelay

// SCI commands used by this minimal driver
namespace
{
    constexpr uint8_t CMD_RESET = 0x06U;
    constexpr uint8_t CMD_RREG  = 0x20U;   // | register address
    constexpr uint8_t CMD_WREG  = 0x40U;   // | register address

    constexpr uint16_t MAX_TRANSFER     = 8U;
    constexpr uint32_t BYTE_TIMEOUT_US  = 200U;   // generous per-byte budget
}

// ─── SCI callback shim ────────────────────────────────────────────────────────
// Registered at R_SCI_Open() time for the SPI (SCI6) channel. Set when the
// synchronous transfer engine raises SCI_EVT_TX_DONE (fires once all bytes
// in the transfer have been clocked in/out).

static volatile bool s_spi_tx_done = false;

extern "C" void ads1263_spi_sci_callback(void* p_args)
{
    const sci_cb_args_t* p = static_cast<const sci_cb_args_t*>(p_args);
    if (p->event == SCI_EVT_TX_DONE)
    {
        s_spi_tx_done = true;
    }
}

Ads1263Spi::Ads1263Spi(sci_hdl_t hdl, volatile uint8_t& cs_podr, uint8_t cs_mask)
    : m_hdl(hdl), m_cs_podr(cs_podr), m_cs_mask(cs_mask)
{
    csHigh();
}

void Ads1263Spi::csLow()
{
    m_cs_podr &= static_cast<uint8_t>(~m_cs_mask);
    R_BSP_SoftwareDelay(1U, BSP_DELAY_MICROSECS);   // tCSS
}

void Ads1263Spi::csHigh()
{
    m_cs_podr |= m_cs_mask;
    R_BSP_SoftwareDelay(1U, BSP_DELAY_MICROSECS);   // tCSH
}

bool Ads1263Spi::transfer(const uint8_t* tx, uint8_t* rx, uint16_t len)
{
    if (len == 0U || len > MAX_TRANSFER)
    {
        return false;
    }

    uint8_t dummy_tx[MAX_TRANSFER] = {};
    uint8_t dummy_rx[MAX_TRANSFER];

    const uint8_t* p_tx = (tx != nullptr) ? tx : dummy_tx;
    uint8_t*       p_rx = (rx != nullptr) ? rx : dummy_rx;

    s_spi_tx_done = false;

    const sci_err_t err = R_SCI_SendReceive(m_hdl,
                                             const_cast<uint8_t*>(p_tx),
                                             p_rx,
                                             len);
    if (err != SCI_SUCCESS)
    {
        return false;
    }

    uint32_t ticks = static_cast<uint32_t>(len) * BYTE_TIMEOUT_US;
    while (!s_spi_tx_done && ticks > 0U)
    {
        R_BSP_SoftwareDelay(1U, BSP_DELAY_MICROSECS);
        --ticks;
    }

    return (ticks > 0U);
}

bool Ads1263Spi::reset()
{
    const uint8_t cmd = CMD_RESET;
    csLow();
    const bool ok = transfer(&cmd, nullptr, 1U);
    csHigh();
    return ok;
}

bool Ads1263Spi::writeReg(uint8_t addr, uint8_t val)
{
    const uint8_t tx[3] = {
        static_cast<uint8_t>(CMD_WREG | addr),
        0x00U,      // count - 1 = 0  →  one register
        val
    };

    csLow();
    const bool ok = transfer(tx, nullptr, 3U);
    csHigh();
    return ok;
}

bool Ads1263Spi::readReg(uint8_t addr, uint8_t& val)
{
    // RREG frame: [0x20|addr] [count-1=0] [dummy]
    // Response:   [ignored  ] [ignored   ] [data ]
    const uint8_t tx[3] = {
        static_cast<uint8_t>(CMD_RREG | addr),
        0x00U,
        0x00U
    };
    uint8_t rx[3] = {};

    csLow();
    const bool ok = transfer(tx, rx, 3U);
    csHigh();

    if (ok)
    {
        val = rx[2];
    }
    return ok;
}
