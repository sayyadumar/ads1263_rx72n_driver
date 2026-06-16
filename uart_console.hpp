#pragma once

#include <cstdint>
#include "r_sci_rx_if.h"

// Minimal blocking UART console wrapper around the Renesas SCI FIT module,
// used here for printing debug output over SCI1.

class UartConsole
{
public:
    explicit UartConsole(sci_hdl_t hdl);

    void print(const char* str);

    // Prints "0x" followed by two upper-case hex digits.
    void printHex8(uint8_t val);

private:
    sci_hdl_t m_hdl;
};
