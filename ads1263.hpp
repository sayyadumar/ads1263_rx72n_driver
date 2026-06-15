#pragma once

#include <cstdint>
#include "r_sci_rx_if.h"

// ─── ADS1263 register map ────────────────────────────────────────────────────

enum class ADS1263Reg : uint8_t
{
    ID       = 0x00,
    POWER    = 0x01,
    IFACE    = 0x02,
    MODE0    = 0x03,
    MODE1    = 0x04,
    MODE2    = 0x05,
    INPMUX   = 0x06,
    OFCAL0   = 0x07,
    OFCAL1   = 0x08,
    OFCAL2   = 0x09,
    FSCAL0   = 0x0A,
    FSCAL1   = 0x0B,
    FSCAL2   = 0x0C,
    IDACMUX  = 0x0D,
    IDACMAG  = 0x0E,
    REFMUX   = 0x0F,
    TDACP    = 0x10,
    TDACN    = 0x11,
    GPIOCON  = 0x12,
    GPIODIR  = 0x13,
    GPIODAT  = 0x14,
    ADC2CFG  = 0x15,
    ADC2MUX  = 0x16,
    ADC2OFC0 = 0x17,
    ADC2OFC1 = 0x18,
    ADC2FSC0 = 0x19,
    ADC2FSC1 = 0x1A,
};

// ─── SPI command bytes ────────────────────────────────────────────────────────

enum class ADS1263Cmd : uint8_t
{
    NOP    = 0x00,
    RESET  = 0x06,
    START1 = 0x08,
    STOP1  = 0x0A,
    START2 = 0x0C,
    STOP2  = 0x0E,
    RDATA1 = 0x12,
    RDATA2 = 0x14,
    RREG   = 0x20,  // | register address
    WREG   = 0x40,  // | register address
};

// ─── Configuration enumerations ───────────────────────────────────────────────

// Analog input multiplexer channels
enum class ADS1263Mux : uint8_t
{
    AIN0     = 0x0,
    AIN1     = 0x1,
    AIN2     = 0x2,
    AIN3     = 0x3,
    AIN4     = 0x4,
    AIN5     = 0x5,
    AIN6     = 0x6,
    AIN7     = 0x7,
    AIN8     = 0x8,
    AIN9     = 0x9,
    AINCOM   = 0xA,
    TEMP_P   = 0xB,  // Internal temperature sensor (+)
    AVDD_4   = 0xC,  // AVDD/4 monitor
    DVDD_4   = 0xD,  // DVDD/4 monitor
    TDAC_P   = 0xE,  // TDAC test positive
    FLOAT    = 0xF,  // Floating / disconnected
};

// ADC1 output data rate (samples per second)
enum class ADS1263Rate : uint8_t
{
    SPS_2_5   = 0x0,
    SPS_5     = 0x1,
    SPS_10    = 0x2,
    SPS_16_6  = 0x3,
    SPS_20    = 0x4,
    SPS_50    = 0x5,
    SPS_60    = 0x6,
    SPS_100   = 0x7,
    SPS_400   = 0x8,
    SPS_1200  = 0x9,
    SPS_2400  = 0xA,
    SPS_4800  = 0xB,
    SPS_7200  = 0xC,
    SPS_14400 = 0xD,
    SPS_19200 = 0xE,
    SPS_38400 = 0xF,
};

// PGA gain
enum class ADS1263Gain : uint8_t
{
    GAIN_1  = 0x0,
    GAIN_2  = 0x1,
    GAIN_4  = 0x2,
    GAIN_8  = 0x3,
    GAIN_16 = 0x4,
    GAIN_32 = 0x5,
};

// ─── IFACE register bits (0x02) ───────────────────────────────────────────────
// Bit 4: TIMEOUT   – SPI timeout enable
// Bit 3: STATUS    – Prepend STATUS byte to RDATA1/2 response
// Bit 2: CRC[1]  ─┐ 00 = off, 01 = checksum, 10 = CRC-16
// Bit 1: CRC[0]  ─┘
static constexpr uint8_t IFACE_STATUS_BIT = (1U << 3);

// STATUS byte bit 6: set when ADC1 has new data since last RDATA1
static constexpr uint8_t STATUS_ADC1_RDY  = (1U << 6);

// ─── ADS1263 driver class ─────────────────────────────────────────────────────

class ADS1263
{
public:
    // hdl        – open SCI handle configured for SPI Mode 1 (CPOL=0, CPHA=1), MSB first
    // cs_podr    – reference to the CS pin's port output data register byte
    // cs_mask    – bit mask of the CS pin within that byte (e.g. 0x02 for bit 1)
    //
    // Example (P31 = PORT3 bit 1):
    //   ADS1263 adc(hdl, PORT3.PODR.BYTE, 0x02);
    ADS1263(sci_hdl_t hdl, volatile uint8_t& cs_podr, uint8_t cs_mask);

    // Reset device and apply initial configuration.
    // Enables STATUS byte in RDATA1 response for DRDY polling.
    // Returns false if the device ID cannot be verified.
    bool begin(ADS1263Rate rate = ADS1263Rate::SPS_100,
               ADS1263Gain gain = ADS1263Gain::GAIN_1);

    // Select differential input pair (positive and negative channels)
    bool setMux(ADS1263Mux pos, ADS1263Mux neg);

    // Set ADC1 output data rate
    bool setRate(ADS1263Rate rate);

    // Set PGA gain (GAIN_1 enables PGA bypass for lowest noise floor)
    bool setGain(ADS1263Gain gain);

    // Start / stop continuous ADC1 conversions
    bool start();
    bool stop();

    // Register a volatile flag that an external IRQ ISR sets when DRDY asserts.
    // When set, read() waits on this flag instead of polling the STATUS byte —
    // more CPU-efficient and lower latency.  Pass nullptr to revert to polling.
    void setDRDYFlag(volatile bool* flag);

    // Read one ADC1 result.
    // If a DRDY flag was registered via setDRDYFlag(), waits on that flag.
    // Otherwise polls the STATUS byte returned by the RDATA1 command.
    // raw: signed 32-bit two's-complement result
    // Returns false on timeout or SPI error.
    bool read(int32_t& raw, uint32_t timeout_ms = 500);

    // Convert a raw 32-bit code to voltage.
    // vref: reference voltage in volts (default = internal 2.5 V)
    static float toVolts(int32_t raw,
                         float vref           = 2.5f,
                         ADS1263Gain gain_sel = ADS1263Gain::GAIN_1);

    // Low-level register access (useful for debugging / advanced config)
    bool    writeReg(ADS1263Reg reg, uint8_t val);
    uint8_t readReg(ADS1263Reg reg);

private:
    sci_hdl_t         m_hdl;
    volatile uint8_t& m_cs_podr;
    uint8_t           m_cs_mask;
    volatile bool*    m_drdy_flag = nullptr;   // set by IRQ ISR; nullptr = poll STATUS

    void csLow();
    void csHigh();

    // Full-duplex SPI exchange; tx/rx may point to the same buffer.
    // Blocks until transfer completes or times out.
    bool spiTransfer(const uint8_t* tx, uint8_t* rx, uint16_t len);

    bool sendCmd(ADS1263Cmd cmd);

    static void delayUs(uint32_t us);
    static void delayMs(uint32_t ms);
};
