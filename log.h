#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialize SCI7 async UART at 115200 8N1 for logging.
// Safe to call multiple times (closes any stale handle from a debugger reset).
void log_init(void);

// Transmit a null-terminated string.  Non-blocking: bytes are enqueued into the
// SCI7 TX queue (80 bytes) and the TXI ISR drains it in the background.
void log_puts(const char *s);

// printf-style logging into a 128-byte stack buffer then log_puts().
void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif
