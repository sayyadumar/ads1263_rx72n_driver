/*
 * SCI7 async UART logger — 115200 8N1
 *
 * R_SCI_Send() in async mode copies bytes into the driver's internal TX byteq
 * (80 bytes, SCI_CFG_CH7_TX_BUFSIZ) and returns immediately; the TXI ISR
 * drains the queue in the background.  This makes all log calls non-blocking.
 *
 * Pin map (from Pin.c / r_sci_rx_pinset.c):
 *   P90  TXD7  (output, PMR set after R_SCI_Open per Pin.c comment)
 *   P92  RXD7  (input,  PMR set at R_Pins_Create time)
 */

#include "log.h"
#include "platform.h"
#include "r_sci_rx_if.h"
#include "r_sci_rx_pinset.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOG_BUF_SIZE 80U    /* fits in one full TX byteq write */

static sci_hdl_t s_log_hdl = NULL;

static void log_sci7_callback(void *p_args)
{
    (void)p_args;
}

void log_init(void)
{
    sci_cfg_t cfg;
    cfg.async.baud_rate    = 115200;
    cfg.async.clk_src      = SCI_CLK_INT;
    cfg.async.data_size    = SCI_DATA_8BIT;
    cfg.async.parity_en    = SCI_PARITY_OFF;
    cfg.async.stop_bits    = SCI_STOPBITS_1;
    cfg.async.int_priority = 4;     /* lower priority than SCI1(3)/IRQ12(3) */

    /* warm-reset: close any stale handle so re-open succeeds */
    if (s_log_hdl != NULL)
    {
        R_SCI_Close(s_log_hdl);
        s_log_hdl = NULL;
    }

    sci_err_t err = R_SCI_Open(SCI_CH7, SCI_MODE_ASYNC, &cfg,
                                log_sci7_callback, &s_log_hdl);

    if (err == SCI_ERR_CH_NOT_CLOSED)
    {
        R_SCI_Close(s_log_hdl);
        err = R_SCI_Open(SCI_CH7, SCI_MODE_ASYNC, &cfg,
                         log_sci7_callback, &s_log_hdl);
    }

    if (err != SCI_SUCCESS)
    {
        s_log_hdl = NULL;
        return;
    }

    /* Enable TXD7 (P90) peripheral mode AFTER R_SCI_Open sets the TE bit.
     * Pin.c configured MPC/PODR/PDR but left PMR commented out for this reason.
     * R_SCI_PinSet_SCI7() writes the same MPC values and then sets PMR. */
    R_SCI_PinSet_SCI7();
}

void log_puts(const char *s)
{
    if (s_log_hdl == NULL || s == NULL)
        return;

    uint16_t len = (uint16_t)strlen(s);
    if (len == 0U)
        return;

    /* Clamp to TX queue size; longer messages are silently truncated.
     * Increase SCI_CFG_CH7_TX_BUFSIZ (r_sci_rx_config.h) for longer strings. */
    if (len > LOG_BUF_SIZE)
        len = (uint16_t)LOG_BUF_SIZE;

    R_SCI_Send(s_log_hdl, (uint8_t *)s, len);
}

void log_printf(const char *fmt, ...)
{
    char buf[LOG_BUF_SIZE + 1U];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log_puts(buf);
}
