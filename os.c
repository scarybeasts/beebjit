#ifdef __linux__

#include "os_lock_linux.c"
#include "os_poller_linux.c"
#include "os_sound_linux.c"
#include "os_window_linux.c"

#else
#error Not yet ported to Windows, Mac, etc.
#endif
