#include "ads1263.hpp"
extern "C" {
#include "r_bsp_common.h"   // R_BSP_SoftwareDelay
}

// ─── SCI callback shim ────────────────────────────────────────────────────────
// Registered at R_SCI_Open() time.  Set s_tx_done once the SCI synchronous
// transfer engine raises SCI_EVT_TX_DONE (fires after all bytes are clocked).

static volatile bool s_tx_done = false;

extern "C" void ads1263_sci_callback(void* p_args)
{
    const sci_cb_args_t* p = static_cast<const sci_cb_args_t*>(p_args);
    if (p->event == SCI_EVT_XFER_DONE)
    {
        s_tx_done = true;
    }
}

// Maximum bytes per single SPI transaction (STATUS + 4 data + headroom)
static constexpr uint16_t MAX_TRANSFER = 16U;

// Per-byte SPI timeout budget (µs): covers 10 MHz clock with margin
static constexpr uint32_t BYTE_TIMEOUT_US = 50U;

// ─── Constructor ──────────────────────────────────────────────────────────────

ADS1263::ADS1263(sci_hdl_t hdl, volatile uint8_t& cs_podr, uint8_t cs_mask)
    : m_hdl(hdl), m_cs_podr(cs_podr), m_cs_mask(cs_mask)
{
    csHigh();   // ensure CS is deasserted at construction
}

// ─── CS control ───────────────────────────────────────────────────────────────

void ADS1263::csLow()
{
    m_cs_podr &= static_cast<uint8_t>(~m_cs_mask);
    delayUs(1);     // tCSS ≥ 25 ns; 1 µs is conservative
}

void ADS1263::csHigh()
{
    m_cs_podr |= m_cs_mask;
    delayUs(1);     // tCSH ≥ 25 ns
}

// ─── Delay helpers ────────────────────────────────────────────────────────────

void ADS1263::delayUs(uint32_t us)
{
    R_BSP_SoftwareDelay(us, BSP_DELAY_MICROSECS);
}

void ADS1263::delayMs(uint32_t ms)
{
    R_BSP_SoftwareDelay(ms, BSP_DELAY_MILLISECS);
}

// ─── SPI transfer ─────────────────────────────────────────────────────────────

bool ADS1263::spiTransfer(const uint8_t* tx, uint8_t* rx, uint16_t len)
{
    if (len == 0U || len > MAX_TRANSFER)
    {
        return false;
    }

    uint8_t dummy_tx[MAX_TRANSFER] = {};    // zeros sent when tx == nullptr
    uint8_t dummy_rx[MAX_TRANSFER];         // sink when rx == nullptr

    const uint8_t* p_tx = (tx != nullptr) ? tx : dummy_tx;
    uint8_t*       p_rx = (rx != nullptr) ? rx : dummy_rx;

    s_tx_done = false;

    sci_err_t err = R_SCI_SendReceive(m_hdl,
                                       const_cast<uint8_t*>(p_tx),
                                       p_rx,
                                       len);
    if (err != SCI_SUCCESS)
    {
        return false;
    }

    // Wait for ISR-driven transfer to complete
    uint32_t ticks = static_cast<uint32_t>(len) * BYTE_TIMEOUT_US;
    while (!s_tx_done && ticks > 0U)
    {
        delayUs(1U);
        --ticks;
    }

    return (ticks > 0U);
}

// ─── Command helper ───────────────────────────────────────────────────────────

bool ADS1263::sendCmd(ADS1263Cmd cmd)
{
    const uint8_t b = static_cast<uint8_t>(cmd);
    csLow();
    const bool ok = spiTransfer(&b, nullptr, 1U);
    csHigh();
    return ok;
}

// ─── Register access ─────────────────────────────────────────────────────────

bool ADS1263::writeReg(ADS1263Reg reg, uint8_t val)
{
    const uint8_t tx[3] = {
        static_cast<uint8_t>(static_cast<uint8_t>(ADS1263Cmd::WREG) |
                              static_cast<uint8_t>(reg)),
        0x00,   // count-1 = 0  →  write exactly 1 register
        val
    };

    csLow();
    const bool ok = spiTransfer(tx, nullptr, 3U);
    csHigh();
    return ok;
}

uint8_t ADS1263::readReg(ADS1263Reg reg)
{
    // RREG frame: [0x20|addr] [count-1=0] [dummy]
    // Response:  [ignored  ] [ignored   ] [data ]
    const uint8_t tx[3] = {
        static_cast<uint8_t>(static_cast<uint8_t>(ADS1263Cmd::RREG) |
                              static_cast<uint8_t>(reg)),
        0x00,
        0x00
    };
    uint8_t rx[3] = {};

    csLow();
    const bool ok = spiTransfer(tx, rx, 3U);
    csHigh();

    return ok ? rx[2] : 0xFFU;
}

// ─── Reset control ────────────────────────────────────────────────────────────

void ADS1263::setResetPin(volatile uint8_t* podr, uint8_t mask)
{
    m_rst_podr = podr;
    m_rst_mask = mask;
}

void ADS1263::reset()
{
    if (m_rst_podr != nullptr)
    {
        // Hardware reset via RESET/PWDN pin (active-low).
        // Datasheet th(RSTL): low >= 4 tCLK (~0.5 µs) resets; must stay < 65536
        // tCLK (~9 ms) or the device enters power-down.  10 µs is safely between.
        *m_rst_podr &= static_cast<uint8_t>(~m_rst_mask);   // assert (low)
        delayUs(10U);
        *m_rst_podr |= m_rst_mask;                           // deassert (high)
    }
    else
    {
        // Software reset command (RESET opcode 0x06).
        sendCmd(ADS1263Cmd::RESET);
    }
}

// ─── Initialisation ───────────────────────────────────────────────────────────

bool ADS1263::begin(ADS1263Rate rate, ADS1263Gain gain)
{
    delayMs(50U);   // POR: datasheet recommends >=50 ms after supplies stabilise

    reset();        // hardware RESET/PWDN pulse if registered, else SW command
    delayMs(5U);    // register/reference settle after reset (th(RSTCM) >= 8 tCLK)

    // Verify device ID – upper 3 bits must be 0b001 (= 1) for ADS1263
    const uint8_t id = readReg(ADS1263Reg::ID);
    if ((id >> 5U) != 0x01U)
    {
        return false;
    }

    // Enable STATUS byte in RDATA1 response so we can poll DRDY without
    // a dedicated DRDY GPIO.  Disable checksum/CRC for simplicity.
    // IFACE = 0x08:  STATUS=1, CRC=00
    if (!writeReg(ADS1263Reg::IFACE, IFACE_STATUS_BIT))
    {
        return false;
    }

    // MODE0: normal operation, no sensor bias, no conversion delay
    if (!writeReg(ADS1263Reg::MODE0, 0x00U))
    {
        return false;
    }

    // MODE1: Sinc1 filter (FILTER=000) — zero-latency, single-cycle settled.
    // Important for channel scanning: after an INPMUX change restarts the
    // conversion, the very next DRDY already carries fully-settled data.
    if (!writeReg(ADS1263Reg::MODE1, 0x00U))
    {
        return false;
    }

    // POWER: enable internal 2.5 V reference (INTREF=1), VBIAS off.
    // Reset default (0x11) already has INTREF=1; written explicitly here.
    if (!writeReg(ADS1263Reg::POWER, 0x01U))
    {
        return false;
    }

    // REFMUX: select the internal 2.5 V reference for ADC1
    // (RMUXP=000 internal-P, RMUXN=000 internal-N). Reset default is 0x00.
    if (!writeReg(ADS1263Reg::REFMUX, 0x00U))
    {
        return false;
    }

    if (!setRate(rate))  { return false; }
    if (!setGain(gain))  { return false; }

    // Default: AIN0(+) vs AIN1(-)
    if (!setMux(ADS1263Mux::AIN0, ADS1263Mux::AIN1))
    {
        return false;
    }

    return true;
}

// ─── Configuration ───────────────────────────────────────────────────────────

bool ADS1263::setMux(ADS1263Mux pos, ADS1263Mux neg)
{
    const uint8_t val =
        static_cast<uint8_t>((static_cast<uint8_t>(pos) << 4U) |
                               static_cast<uint8_t>(neg));
    return writeReg(ADS1263Reg::INPMUX, val);
}

bool ADS1263::setRate(ADS1263Rate rate)
{
    // MODE2[3:0] = DR;  preserve MODE2[7:4] (BYPASS + GAIN)
    uint8_t mode2 = readReg(ADS1263Reg::MODE2);
    mode2 = static_cast<uint8_t>((mode2 & 0xF0U) |
                                   (static_cast<uint8_t>(rate) & 0x0FU));
    return writeReg(ADS1263Reg::MODE2, mode2);
}

bool ADS1263::setGain(ADS1263Gain gain)
{
    // MODE2[7]   = BYPASS (1 = bypass PGA)
    // MODE2[6:4] = GAIN
    // MODE2[3:0] = DR  (preserved)
    uint8_t mode2 = readReg(ADS1263Reg::MODE2);
    mode2 &= 0x0FU;    // clear upper nibble

    if (static_cast<uint8_t>(gain) == 0U)
    {
        mode2 |= 0x80U;     // BYPASS=1, GAIN=000
    }
    else
    {
        mode2 |= static_cast<uint8_t>(static_cast<uint8_t>(gain) << 4U);
    }

    return writeReg(ADS1263Reg::MODE2, mode2);
}

// ─── Conversion control ───────────────────────────────────────────────────────

bool ADS1263::start()
{
    return sendCmd(ADS1263Cmd::START1);
}

bool ADS1263::stop()
{
    return sendCmd(ADS1263Cmd::STOP1);
}

// ─── DRDY flag (optional IRQ path) ───────────────────────────────────────────

void ADS1263::setDRDYFlag(volatile bool* flag)
{
    m_drdy_flag = flag;
}

// ─── Data read ────────────────────────────────────────────────────────────────

bool ADS1263::read(int32_t& raw, uint32_t timeout_ms)
{
    // RDATA1 *by command* with STATUS byte enabled (IFACE.STATUS=1, CRC off).
    // Per datasheet Fig 9-44, the command byte clocks out a don't-care byte
    // first, THEN the STATUS byte, THEN 4 data bytes — 6 bytes total:
    //   send:  [RDATA1][dummy][dummy][dummy][dummy][dummy]
    //   recv:  [dontcr][STATUS][ D3  ][ D2  ][ D1  ][ D0  ]
    const uint8_t tx[6] = {
        static_cast<uint8_t>(ADS1263Cmd::RDATA1),
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    uint8_t rx[6] = {};

    auto assemble = [&]() -> int32_t {
        const uint32_t u =
            (static_cast<uint32_t>(rx[2]) << 24U) |   // D3 (MSB)
            (static_cast<uint32_t>(rx[3]) << 16U) |   // D2
            (static_cast<uint32_t>(rx[4]) <<  8U) |   // D1
             static_cast<uint32_t>(rx[5]);            // D0 (LSB)
        return static_cast<int32_t>(u);
    };

    if (m_drdy_flag != nullptr)
    {
        // ── IRQ path: wait for the ISR flag, then issue one RDATA1 ──────────
        // Clear any stale assertion before waiting so we don't read old data.
        *m_drdy_flag = false;

        uint32_t elapsed = 0U;
        while (!(*m_drdy_flag) && elapsed <= timeout_ms)
        {
            delayMs(1U);
            ++elapsed;
        }
        if (elapsed > timeout_ms) { return false; }

        csLow();
        const bool ok = spiTransfer(tx, rx, 6U);
        csHigh();
        if (!ok) { return false; }

        m_last_status = rx[1];
        raw = assemble();
        return true;
    }

    // ── Polling path: loop RDATA1 until STATUS.ADC1 (bit 6) is set ──────────
    // The STATUS byte is rx[1]; bit 6 = new ADC1 data since the last read.
    uint32_t elapsed_ms = 0U;
    while (elapsed_ms <= timeout_ms)
    {
        csLow();
        const bool ok = spiTransfer(tx, rx, 6U);
        csHigh();

        if (!ok) { return false; }

        m_last_status = rx[1];
        if ((rx[1] & STATUS_ADC1_RDY) != 0U)
        {
            raw = assemble();
            return true;
        }

        delayMs(1U);
        ++elapsed_ms;
    }

    return false;   // timeout
}

// ─── Voltage conversion ───────────────────────────────────────────────────────

float ADS1263::toVolts(int32_t raw, float vref, ADS1263Gain gain_sel)
{
    static constexpr uint8_t k_gain_lut[] = {1U, 2U, 4U, 8U, 16U, 32U};

    const uint8_t idx  = static_cast<uint8_t>(gain_sel);
    const uint8_t gain = (idx < 6U) ? k_gain_lut[idx] : 1U;

    // Full-scale range = ±(Vref / gain)
    // LSB weight = 2 * Vref / (gain * 2^32)
    return (static_cast<float>(raw) / 2147483648.0f) * (vref / static_cast<float>(gain));
}
