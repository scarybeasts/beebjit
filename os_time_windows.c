#include "os_time.h"

#include "util.h"

#include <stdio.h>
#include <windows.h>

int s_frequency_queried;
uint64_t s_frequency;

uint64_t
os_time_get_us() {
  BOOL ret;
  LARGE_INTEGER li;
  uint64_t value;

  if (!s_frequency_queried) {
    ret = QueryPerformanceFrequency(&li);
    if (ret == 0) {
      util_bail("QueryPerformanceFrequency failed");
    }
    if (li.QuadPart < 1000000) {
      util_bail("QueryPerformanceFrequency has lamentable resolution");
    }

    s_frequency_queried = 1;
    s_frequency = li.QuadPart;
  }

  ret = QueryPerformanceCounter(&li);
  if (ret == 0) {
    util_bail("QueryPerformanceCounter failed");
  }

  value = li.QuadPart;
  value *= ((double) 1000000.0 / s_frequency);

  return value;
}

void
os_time_sleep_us(uint64_t us) {
  (void) us;
}
