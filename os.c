#if defined __linux__

#include "os_poller_linux.c"
#include "os_sound_linux.c"
#include "os_thread_linux.c"
#include "os_window_linux.c"

#elif defined __APPLE__

#include "os_poller_linux.c"
#include "os_sound_null.c"
#include "os_thread_mac.c"
#include "os_window_linux.c"

#else

#error Not yet ported to Windows, Mac, etc.

#endif
