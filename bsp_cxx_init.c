/*
 * _CALL_INIT — GNURX C++ global constructor shim.
 *
 * resetprg.c calls this when BSP_CFG_CPLUSPLUS == 1.  CC-RX provides it
 * natively; GNURX does not.  All C++ objects in this project are stack-local
 * (declared inside main()), so there are no global constructors to run.
 * If a C++ global/static object is ever added, add a proper init_array walk
 * here, or switch to a newlib crt0 that handles it automatically.
 */
void _CALL_INIT(void) {}

/* ── newlib-nano system stubs (bare-metal) ────────────────────────────────
 * These are pulled in by libc_nano.a functions like abort(), raise(), etc.
 * On a bare-metal MCU they just return an error or trap. */

void _exit(int status)
{
    (void)status;
    while (1) { /* trap */ }
}

int kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

int getpid(void)
{
    return 1;
}
