#if defined(__linux__)

/* Must occur before other includes. */
#define _GNU_SOURCE /* For REG_RIP in ucontext.h, os_fault_posix.c */

#include "os_alloc_posix.c"
#include "os_channel_posix.c"
#include "os_fault_posix.c"
#include "os_poller_linux.c"
#include "os_sound_linux.c"
#include "os_terminal_posix.c"
#include "os_thread_linux.c"
#include "os_time_posix.c"
#include "os_window_x11.c"
#include "os_x11_keys_linux.c"

#elif defined(WIN32)

#include "os_alloc_windows.c"
#include "os_channel_windows.c"
#include "os_fault_windows.c"
#include "os_poller_windows.c"
#include "os_sound_windows.c"
#include "os_terminal_windows.c"
#include "os_thread_windows.c"
#include "os_time_windows.c"
#include "os_window_windows.c"

#else

#error Not yet ported to Windows, Mac, etc.

#endif
