#ifdef __linux__

/* Must occur before other includes. */
#define _GNU_SOURCE /* For REG_RIP in ucontext.h, os_fault_posix.c */

#include "os_alloc_posix.c"
#include "os_fault_posix.c"
#include "os_poller_linux.c"
#include "os_sound_linux.c"
#include "os_thread_linux.c"
#include "os_window_x11.c"
#include "os_x11_keys_linux.c"

#else
#error Not yet ported to Windows, Mac, etc.
#endif
