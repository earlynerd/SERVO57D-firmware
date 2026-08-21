/*
 * The vendor startup calls newlib's __libc_init_array while the firmware links
 * without the toolchain's generic CRT startup. Newlib therefore requires these
 * two lifecycle hooks; this bare-metal target has no process-level init/fini
 * work to perform in them.
 */
void _init(void)
{
    return;
}

void _fini(void)
{
    return;
}
