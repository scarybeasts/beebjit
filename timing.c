#include "timing.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_timing_num_timers = 8,
};

struct timing_struct {
  size_t max_timer;
  void (*p_callbacks[k_timing_num_timers])(void*);
  void* p_objects[k_timing_num_timers];
  int64_t timings[k_timing_num_timers];
  uint8_t running[k_timing_num_timers];

  uint64_t total_timer_ticks;
  int64_t countdown;
};

struct timing_struct*
timing_create() {
  struct timing_struct* p_timing = malloc(sizeof(struct timing_struct));
  if (p_timing == NULL) {
    errx(1, "couldn't allocate timing_struct");
  }

  (void) memset(p_timing, '\0', sizeof(struct timing_struct));

  p_timing->max_timer = 0;
  p_timing->total_timer_ticks = 0;
  p_timing->countdown = INT64_MAX;

  return p_timing;
}

void
timing_destroy(struct timing_struct* p_timing) {
  free(p_timing);
}

static void
timing_recalculate(struct timing_struct* p_timing) {
  size_t i;
  /* Default is never. */
  int64_t countdown = INT64_MAX;
  size_t max_timer = p_timing->max_timer;

  for (i = 0; i < max_timer; ++i) {
    if (!p_timing->running[i]) {
      continue;
    }
    if (p_timing->timings[i] < countdown) {
      countdown = p_timing->timings[i];
    }
  }

  p_timing->countdown = countdown;
}

size_t
timing_register_timer(struct timing_struct* p_timing,
                      void* p_callback,
                      void* p_object) {
  size_t i;
  for (i = 0; i < k_timing_num_timers; ++i) {
    if (p_timing->p_callbacks[i] == NULL) {
      break;
    }
  }
  assert(i < k_timing_num_timers);

  p_timing->max_timer = (i + 1);

  p_timing->p_callbacks[i] = p_callback;
  p_timing->p_objects[i] = p_object;
  p_timing->timings[i] = 0;
  p_timing->running[i] = 0;

  return i;
}

int64_t
timing_start_timer(struct timing_struct* p_timing, size_t id, int64_t time) {
  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);
  assert(p_timing->p_callbacks[id] != NULL);
  assert(!p_timing->running[id]);

  p_timing->timings[id] = time;
  p_timing->running[id] = 1;

  /* Don't need to do a full recalculate, can just see if the timer is expiring
   * sooner than the current soonest.
   */
  if (time < p_timing->countdown) {
    p_timing->countdown = time;
  }

  return p_timing->countdown;
}

int64_t
timing_stop_timer(struct timing_struct* p_timing, size_t id) {
  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);
  assert(p_timing->p_callbacks[id] != NULL);
  assert(p_timing->running[id]);

  p_timing->running[id] = 0;

  timing_recalculate(p_timing);

  return p_timing->countdown;
}

int
timing_timer_is_running(struct timing_struct* p_timing, size_t id) {
  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);

  return p_timing->running[id];
}

int64_t
timing_increase_timer(int64_t* p_new_value,
                      struct timing_struct* p_timing,
                      size_t id,
                      int64_t time) {
  int64_t value = p_timing->timings[id];

  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);
  assert(p_timing->p_callbacks[id] != NULL);

  value += time;
  if (p_new_value) {
    *p_new_value = value;
  }
  p_timing->timings[id] = value;

  timing_recalculate(p_timing);

  return p_timing->countdown;
}

int64_t
timing_get_countdown(struct timing_struct* p_timing) {
  return p_timing->countdown;
}

uint64_t
timing_update_countdown(struct timing_struct* p_timing, int64_t countdown) {
  size_t i;

  size_t max_timer = p_timing->max_timer;
  uint64_t delta = (p_timing->countdown - countdown);

  p_timing->total_timer_ticks += delta;
  countdown = INT64_MAX;

  for (i = 0; i < max_timer; ++i) {
    int64_t value;
    uint8_t running = p_timing->running[i];

    if (!running) {
      continue;
    }
    value = p_timing->timings[i];
    value -= delta;
    p_timing->timings[i] = value;
    if (value < countdown) {
      countdown = value;
    }
  }

  p_timing->countdown = countdown;

  return delta;
}

int64_t
timing_trigger_callbacks(struct timing_struct* p_timing) {
  size_t i;

  size_t max_timer = p_timing->max_timer;

  for (i = 0; i < max_timer; ++i) {
    int64_t value;
    uint8_t running = p_timing->running[i];

    if (!running) {
      continue;
    }
    value = p_timing->timings[i];
    if (value <= 0) {
      void (*p_callback)(void*) = p_timing->p_callbacks[i];
      p_callback(p_timing->p_objects[i]);
    }
  }

  timing_recalculate(p_timing);

  return p_timing->countdown;
}
