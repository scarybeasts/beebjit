#include "os_time.h"

#include "util.h"

#include <inttypes.h>
#include <stdio.h>
#include <windows.h>

int s_frequency_queried;
uint64_t s_frequency;

struct os_time_sleeper {
  HANDLE handle;
};

void
os_time_setup_hi_res(void) {
  TIMECAPS time_caps;

  MMRESULT ret = timeGetDevCaps(&time_caps, sizeof(time_caps));
  if (ret != MMSYSERR_NOERROR) {
    util_bail("timeGetDevCaps failed");
  }
  log_do_log(k_log_audio,
             k_log_info,
             "Windows timing capabilities min %"PRIu32" max %"PRIu32,
             time_caps.wPeriodMin,
             time_caps.wPeriodMax);

  ret = timeBeginPeriod(time_caps.wPeriodMin);
  if (ret != TIMERR_NOERROR) {
    log_do_log(k_log_audio, k_log_warning, "timeBeginPeriod failed");
  }
}

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

struct os_time_sleeper*
os_time_create_sleeper(void) {
  HANDLE handle;

  struct os_time_sleeper* p_ret =
      util_mallocz(sizeof(struct os_time_sleeper));

  handle = CreateWaitableTimer(NULL, TRUE, "sleeper");
  if (handle == NULL) {
    util_bail("CreateWaitableTimer failed");
  }

  p_ret->handle = handle;

  return p_ret;
}

void
os_time_free_sleeper(struct os_time_sleeper* p_sleeper) {
  BOOL ret = CloseHandle(p_sleeper->handle);
  if (ret == 0) {
    util_bail("CloseHandle failed");
  }

  util_free(p_sleeper);
}

void
os_time_sleeper_sleep_us(struct os_time_sleeper* p_sleeper, uint64_t us) {
  BOOL set_ret;
  DWORD wait_ret;
  LARGE_INTEGER li;

  HANDLE handle = p_sleeper->handle;

  /* Unit of timer is 100ns. */
  li.QuadPart = -(int64_t) (us * 10);

  set_ret = SetWaitableTimer(handle, &li, 0, NULL, NULL, FALSE);
  if (set_ret == 0) {
    util_bail("SetWaitableTimer failed");
  }

  wait_ret = WaitForSingleObject(handle, INFINITE);
  if (wait_ret != WAIT_OBJECT_0) {
    util_bail("WaitForSingleObject failed");
  }
}
