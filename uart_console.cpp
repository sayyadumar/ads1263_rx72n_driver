#include "uart_console.hpp"

namespace
{
    // Sized for the longest single call this console makes ("0xFF" + slack).
    constexpr uint32_t TX_TIMEOUT_LOOPS = 1000000UL;
}

// ─── SCI callback shim ────────────────────────────────────────────────────────
// Registered at R_SCI_Open() time for the UART (SCI1) channel.

static volatile bool s_uart_tx_done = false;

extern "C" void uart_console_sci_callback(void* p_args)
{
    const sci_cb_args_t* p = static_cast<const sci_cb_args_t*>(p_args);
    if (p->event == SCI_EVT_TX_DONE)
    {
        s_uart_tx_done = true;
    }
}

UartConsole::UartConsole(sci_hdl_t hdl)
    : m_hdl(hdl)
{
}

void UartConsole::print(const char* str)
{
    uint16_t len = 0U;
    while (str[len] != '\0')
    {
        ++len;
    }
    if (len == 0U)
    {
        return;
    }

    s_uart_tx_done = false;

    const sci_err_t err = R_SCI_Send(m_hdl,
                                      reinterpret_cast<uint8_t*>(const_cast<char*>(str)),
                                      len);
    if (err != SCI_SUCCESS)
    {
        return;
    }

    uint32_t loops = TX_TIMEOUT_LOOPS;
    while (!s_uart_tx_done && loops > 0U)
    {
        --loops;
    }
}

void UartConsole::printHex8(uint8_t val)
{
    static const char k_hex[] = "0123456789ABCDEF";
    char buf[5] = { '0', 'x', k_hex[(val >> 4U) & 0x0FU], k_hex[val & 0x0FU], '\0' };
    print(buf);
}
