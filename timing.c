#include "timing.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_timing_num_timers = 16,
};

struct timer_struct {
  void (*p_callback)(void*);
  void* p_object;
  int64_t value;
  int ticking;
  int firing;
};

struct timing_struct {
  uint32_t scale_factor;
  uint32_t max_timer;
  struct timer_struct timers[k_timing_num_timers];

  uint64_t total_timer_ticks;
  int64_t countdown;
};

struct timing_struct*
timing_create(uint32_t scale_factor) {
  struct timing_struct* p_timing = malloc(sizeof(struct timing_struct));
  if (p_timing == NULL) {
    errx(1, "couldn't allocate timing_struct");
  }

  (void) memset(p_timing, '\0', sizeof(struct timing_struct));

  p_timing->scale_factor = scale_factor;
  p_timing->max_timer = 0;
  p_timing->total_timer_ticks = 0;
  p_timing->countdown = INT64_MAX;

  return p_timing;
}

void
timing_destroy(struct timing_struct* p_timing) {
  free(p_timing);
}

uint64_t
timing_get_total_timer_ticks(struct timing_struct* p_timing) {
  return p_timing->total_timer_ticks;
}

uint64_t
timing_get_scaled_total_timer_ticks(struct timing_struct* p_timing) {
  return (p_timing->total_timer_ticks / p_timing->scale_factor);
}

static void
timing_recalculate(struct timing_struct* p_timing) {
  uint32_t i;
  int64_t value;

  /* Default is never. */
  int64_t countdown = INT64_MAX;
  uint32_t max_timer = p_timing->max_timer;

  for (i = 0; i < max_timer; ++i) {
    if (!p_timing->timers[i].ticking || !p_timing->timers[i].firing) {
      continue;
    }
    value = p_timing->timers[i].value;
    if (value < countdown) {
      countdown = value;
    }
  }

  p_timing->countdown = countdown;
}

uint32_t
timing_register_timer(struct timing_struct* p_timing,
                      void* p_callback,
                      void* p_object) {
  uint32_t i;
  struct timer_struct* p_timer;

  assert(p_callback != NULL);

  for (i = 0; i < k_timing_num_timers; ++i) {
    if (p_timing->timers[i].p_callback == NULL) {
      break;
    }
  }
  if (i == k_timing_num_timers) {
    errx(1, "out of timer ids");
  }

  p_timing->max_timer = (i + 1);

  p_timer = &p_timing->timers[i];

  p_timer->p_callback = p_callback;
  p_timer->p_object = p_object;
  p_timer->value = INT64_MAX;
  p_timer->ticking = 0;
  p_timer->firing = 1;

  return i;
}

int64_t
timing_start_timer(struct timing_struct* p_timing, uint32_t id) {
  int64_t value;

  assert(id < k_timing_num_timers);
  value = p_timing->timers[id].value;
  return timing_start_timer_with_value(p_timing, id, value);
}

int64_t
timing_start_timer_with_value(struct timing_struct* p_timing,
                              uint32_t id,
                              int64_t time) {
  struct timer_struct* p_timer;

  assert(id < k_timing_num_timers);

  p_timer = &p_timing->timers[id];
  assert(p_timer->p_callback != NULL);
  assert(!p_timer->ticking);

  time *= p_timing->scale_factor;

  p_timer->value = time;
  p_timer->ticking = 1;

  /* Don't need to do a full recalculate, can just see if the timer is expiring
   * sooner than the current soonest.
   */
  if (p_timer->firing && (time < p_timing->countdown)) {
    p_timing->countdown = time;
  }

  return p_timing->countdown;
}

int64_t
timing_stop_timer(struct timing_struct* p_timing, uint32_t id) {
  struct timer_struct* p_timer;

  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);

  p_timer = &p_timing->timers[id];
  assert(p_timer->p_callback != NULL);
  assert(p_timer->ticking);

  p_timer->ticking = 0;

  if (p_timer->firing) {
    timing_recalculate(p_timing);
  }

  return p_timing->countdown;
}

int
timing_timer_is_running(struct timing_struct* p_timing, uint32_t id) {
  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);

  return p_timing->timers[id].ticking;
}

int64_t
timing_get_timer_value(struct timing_struct* p_timing, uint32_t id) {
  int64_t ret;

  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);

  ret = p_timing->timers[id].value;
  ret /= p_timing->scale_factor;
  return ret;
}

int64_t
timing_set_timer_value(struct timing_struct* p_timing,
                       uint32_t id,
                       int64_t time) {
  struct timer_struct* p_timer;

  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);

  p_timer = &p_timing->timers[id];
  assert(p_timer->p_callback != NULL);

  time *= p_timing->scale_factor;

  p_timer->value = time;

  /* TODO: can further optimize this away under certain circumstances? */
  if (p_timer->firing) {
    timing_recalculate(p_timing);
  }

  return p_timing->countdown;
}

int64_t
timing_adjust_timer_value(struct timing_struct* p_timing,
                          int64_t* p_new_value,
                          uint32_t id,
                          int64_t delta) {
  int64_t new_time;
  struct timer_struct* p_timer;

  uint32_t scale_factor = p_timing->scale_factor;

  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);

  p_timer = &p_timing->timers[id];
  assert(p_timer->p_callback != NULL);

  delta *= scale_factor;

  new_time = (p_timer->value + delta);

  if (p_new_value) {
    *p_new_value = (new_time / scale_factor);
  }

  p_timer->value = new_time;

  /* TODO: can further optimize this away under certain circumstances? */
  if (p_timer->firing) {
    timing_recalculate(p_timing);
  }

  return p_timing->countdown;
}

int
timing_get_firing(struct timing_struct* p_timing, uint32_t id) {
  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);

  return p_timing->timers[id].firing;
}

int64_t
timing_set_firing(struct timing_struct* p_timing, uint32_t id, int firing) {
  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);

  p_timing->timers[id].firing = firing;

  /* TODO: can optimize. */
  timing_recalculate(p_timing);

  return p_timing->countdown;
}

int64_t
timing_get_countdown(struct timing_struct* p_timing) {
  return p_timing->countdown;
}

static void
timing_do_advance_time(struct timing_struct* p_timing, uint64_t delta) {
  uint32_t i;

  uint32_t max_timer = p_timing->max_timer;

  p_timing->total_timer_ticks += delta;

  /* Pass 1: update all timers with their correct new value. */
  for (i = 0; i < max_timer; ++i) {
    struct timer_struct* p_timer = &p_timing->timers[i];
    if (!p_timer->ticking) {
      continue;
    }

    p_timer->value -= delta;
  }

  /* Pass 2: fire any timers. */
  for (i = 0; i < max_timer; ++i) {
    int64_t value;

    struct timer_struct* p_timer = &p_timing->timers[i];

    if (!p_timer->ticking || !p_timer->firing) {
      continue;
    }

    value = p_timer->value;
    /* Callers of timing_do_advance_time() are required to expire active timers
     * exactly on time.
     */
    assert(value >= 0);
    if (value == 0) {
      p_timer->p_callback(p_timer->p_object);
      assert(!p_timer->ticking || !p_timer->firing || (p_timer->value > 0));
    }
  }

  timing_recalculate(p_timing);
}

int64_t
timing_advance_time(struct timing_struct* p_timing, int64_t countdown) {
  uint64_t sub_delta;

  uint64_t orig_delta = (p_timing->countdown - countdown);
  uint64_t delta = orig_delta;

  assert(countdown <= p_timing->countdown);

  sub_delta = p_timing->countdown;

  while (delta) {
    if (sub_delta > delta) {
      /* TODO: optimization, can return if we know nothing expires. */
      sub_delta = delta;
    }
    timing_do_advance_time(p_timing, sub_delta);
    delta -= sub_delta;

    sub_delta = p_timing->countdown;
    assert(((int64_t) sub_delta) > 0);
  }

  return p_timing->countdown;
}

#include "test-timing.c"
