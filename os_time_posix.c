#include "os_time.h"

#include "util.h"

#include <errno.h>
#include <time.h>

uint64_t
os_time_get_us() {
  struct timespec ts;

  int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (ret != 0) {
    util_bail("clock_gettime failed");
  }

  return ((ts.tv_sec * (uint64_t) 1000000) + (ts.tv_nsec / 1000));
}

struct os_time_sleeper*
os_time_create_sleeper(void) {
  return NULL;
}

void
os_time_free_sleeper(struct os_time_sleeper* p_sleeper) {
  (void) p_sleeper;
}

void
os_time_sleeper_sleep_us(struct os_time_sleeper* p_sleeper, uint64_t us) {
  int ret;
  struct timespec ts;

  (void) p_sleeper;

  ts.tv_sec = (us / 1000000);
  ts.tv_nsec = ((us % 1000000) * 1000);

  do {
    ret = nanosleep(&ts, &ts);
    if (ret != 0) {
      if (errno == EINTR) {
        continue;
      }
      util_bail("nanosleep failed");
    }
  } while (ret != 0);
}
