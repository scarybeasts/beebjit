#if defined(__linux__) || __APPLE__

/* Must occur before other includes. */
#define _GNU_SOURCE /* For REG_RIP in ucontext.h, os_fault_posix.c */

#include "os_alloc_posix.c"
#include "os_channel_posix.c"
#include "os_fault_posix.c"
#include "os_poller_posix.c"
#include "os_terminal_posix.c"
#include "os_thread_posix.c"
#include "os_time_posix.c"
#if __APPLE__
#include "os_lock_macos.c"
#else
#include "os_lock_posix.c"
#endif
#if defined(BEEBJIT_HEADLESS) || __APPLE__
#include "os_sound_null.c"
#include "os_window_null.c"
#else
#include "os_sound_linux.c"
#include "os_window_x11.c"
#include "os_x11_keys_linux.c"
#endif
#if defined(__x86_64__)
#include "os_fault_platform_x64.c"
#elif defined(__aarch64__)
#include "os_fault_platform_arm64.c"
#else
#include "os_fault_platform_null.c"
#endif

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

#error Not yet ported to this system.

#endif
