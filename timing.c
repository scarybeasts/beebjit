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
  struct timer_struct* p_expiry_prev;
  struct timer_struct* p_expiry_next;
};

struct timing_struct {
  uint32_t scale_factor;
  uint32_t max_timer;
  struct timer_struct timers[k_timing_num_timers];

  uint64_t total_timer_ticks;
  struct timer_struct* p_expiry_head;
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
  p_timing->p_expiry_head = NULL;

  return p_timing;
}

void
timing_destroy(struct timing_struct* p_timing) {
  free(p_timing);
}

inline int64_t
timing_get_countdown(struct timing_struct* p_timing) {
  struct timer_struct* p_expiry_head = p_timing->p_expiry_head;

  if (p_expiry_head != NULL) {
    return p_expiry_head->value;
  } else {
    return INT64_MAX;
  }
}

uint64_t
timing_get_total_timer_ticks(struct timing_struct* p_timing) {
  return p_timing->total_timer_ticks;
}

uint64_t
timing_get_scaled_total_timer_ticks(struct timing_struct* p_timing) {
  return (p_timing->total_timer_ticks / p_timing->scale_factor);
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
  p_timer->p_expiry_prev = NULL;
  p_timer->p_expiry_next = NULL;

  return i;
}

static void
timing_insert_expiring_timer(struct timing_struct* p_timing,
                             struct timer_struct* p_timer) {
  struct timer_struct* p_iter = p_timing->p_expiry_head;
  struct timer_struct* p_prev = NULL;

  assert(p_timer->p_expiry_prev == NULL);
  assert(p_timer->p_expiry_next == NULL);
  assert(p_timer->ticking);
  assert(p_timer->firing);

  while ((p_iter != NULL) && (p_timer->value > p_iter->value)) {
    p_prev = p_iter;
    p_iter = p_iter->p_expiry_next;
  }

  p_timer->p_expiry_prev = p_prev;
  if (p_prev == NULL) {
    p_timer->p_expiry_next = p_timing->p_expiry_head;
    if (p_timing->p_expiry_head) {
      p_timing->p_expiry_head->p_expiry_prev = p_timer;
    }
    p_timing->p_expiry_head = p_timer;
  } else {
    p_timer->p_expiry_next = p_prev->p_expiry_next;
    if (p_prev->p_expiry_next) {
      p_prev->p_expiry_next->p_expiry_prev = p_timer;
    }
    p_prev->p_expiry_next = p_timer;
  }
}

static void
timing_remove_expiring_timer(struct timing_struct* p_timing,
                             struct timer_struct* p_timer) {
  if (p_timer->p_expiry_prev) {
    p_timer->p_expiry_prev->p_expiry_next = p_timer->p_expiry_next;
  } else {
    assert(p_timer == p_timing->p_expiry_head);
    p_timing->p_expiry_head = p_timer->p_expiry_next;
  }
  if (p_timer->p_expiry_next) {
    p_timer->p_expiry_next->p_expiry_prev = p_timer->p_expiry_prev;
  }

  p_timer->p_expiry_prev = NULL;
  p_timer->p_expiry_next = NULL;
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

  if (p_timer->firing) {
    timing_insert_expiring_timer(p_timing, p_timer);
  }

  return timing_get_countdown(p_timing);
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
    timing_remove_expiring_timer(p_timing, p_timer);
  }

  return timing_get_countdown(p_timing);
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

  if (p_timer->ticking && p_timer->firing) {
    timing_remove_expiring_timer(p_timing, p_timer);
    timing_insert_expiring_timer(p_timing, p_timer);
  }

  return timing_get_countdown(p_timing);
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

  if (p_timer->ticking && p_timer->firing) {
    timing_remove_expiring_timer(p_timing, p_timer);
    timing_insert_expiring_timer(p_timing, p_timer);
  }

  return timing_get_countdown(p_timing);
}

int
timing_get_firing(struct timing_struct* p_timing, uint32_t id) {
  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);

  return p_timing->timers[id].firing;
}

int64_t
timing_set_firing(struct timing_struct* p_timing, uint32_t id, int firing) {
  struct timer_struct* p_timer;

  int firing_changed = 0;

  assert(id < k_timing_num_timers);
  assert(id < p_timing->max_timer);

  p_timer = &p_timing->timers[id];
  if (firing != p_timer->firing) {
    firing_changed = 1;
    p_timer->firing = firing;
  }

  if (p_timer->ticking && firing_changed) {
    if (firing) {
      timing_insert_expiring_timer(p_timing, p_timer);
    } else {
      timing_remove_expiring_timer(p_timing, p_timer);
    }
  }

  return timing_get_countdown(p_timing);
}

static void
timing_do_advance_time(struct timing_struct* p_timing, uint64_t delta) {
  uint32_t i;
  struct timer_struct* p_timer;

  uint32_t max_timer = p_timing->max_timer;

  p_timing->total_timer_ticks += delta;

  /* Pass 1: update all timers with their correct new value. */
  for (i = 0; i < max_timer; ++i) {
    p_timer = &p_timing->timers[i];
    if (!p_timer->ticking) {
      continue;
    }

    p_timer->value -= delta;
  }

  /* Pass 2: fire any timers. */
  p_timer = p_timing->p_expiry_head;
  while ((p_timer != NULL) && (p_timer->value <= 0)) {
    struct timer_struct* p_old_timer = p_timer;

    assert(p_old_timer->ticking);
    assert(p_old_timer->firing);

    /* Callers of timing_do_advance_time() are required to expire active timers
     * exactly on time.
     */
    assert(p_timer->value == 0);
    /* Load the next timer, as calling the callback is likely to trash it. */
    p_timer = p_timer->p_expiry_next;
    p_old_timer->p_callback(p_old_timer->p_object);
    assert(!p_old_timer->ticking ||
           !p_old_timer->firing ||
           (p_old_timer->value > 0));
  }
}

int64_t
timing_advance_time(struct timing_struct* p_timing, int64_t countdown_target) {
  uint64_t sub_delta;

  int64_t countdown = timing_get_countdown(p_timing);
  uint64_t orig_delta = (countdown - countdown_target);
  uint64_t delta = orig_delta;

  assert(countdown_target <= countdown);

  sub_delta = countdown;

  while (delta) {
    if (sub_delta > delta) {
      /* TODO: optimization, can return if we know nothing expires. */
      sub_delta = delta;
    }
    timing_do_advance_time(p_timing, sub_delta);
    delta -= sub_delta;

    sub_delta = timing_get_countdown(p_timing);
    assert(((int64_t) sub_delta) > 0);
  }

  return timing_get_countdown(p_timing);
}

#include "test-timing.c"
