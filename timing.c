#include "timing.h"

#include "util.h"

#include <assert.h>

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
  struct timer_struct* p_ticking_prev;
  struct timer_struct* p_ticking_next;
};

struct timing_struct {
  uint32_t scale_factor;
  struct timer_struct timers[k_timing_num_timers];

  uint64_t total_timer_ticks;
  struct timer_struct* p_expiry_head;
  struct timer_struct* p_ticking_head;

  uint64_t next_timer_expiry;
  uint64_t countdown;
  uint32_t num_timers;
};

struct timing_struct*
timing_create(uint32_t scale_factor) {
  struct timing_struct* p_timing = util_mallocz(sizeof(struct timing_struct));

  p_timing->scale_factor = scale_factor;
  p_timing->total_timer_ticks = 0;
  p_timing->p_expiry_head = NULL;
  p_timing->p_ticking_head = NULL;

  p_timing->next_timer_expiry = INT64_MAX;
  p_timing->countdown = INT64_MAX;

  return p_timing;
}

void
timing_destroy(struct timing_struct* p_timing) {
  util_free(p_timing);
}

void
timing_reset_total_timer_ticks(struct timing_struct* p_timing) {
  p_timing->total_timer_ticks = 0;
}

static inline uint64_t
timing_get_countdown_adjustment(struct timing_struct* p_timing) {
  assert(p_timing->next_timer_expiry >= p_timing->countdown);
  return (p_timing->next_timer_expiry - p_timing->countdown);
}

static uint64_t
timing_update_counts(struct timing_struct* p_timing) {
  uint64_t countdown;
  uint64_t next_timer_expiry;

  struct timer_struct* p_expiry_head = p_timing->p_expiry_head;
  uint64_t adjustment = timing_get_countdown_adjustment(p_timing);

  if (p_expiry_head == NULL) {
    next_timer_expiry = INT64_MAX;
  } else {
    next_timer_expiry = p_expiry_head->value;
  }

  countdown = (next_timer_expiry - adjustment);

  p_timing->next_timer_expiry = next_timer_expiry;
  p_timing->countdown = countdown;

  return countdown;
}

inline int64_t
timing_get_countdown(struct timing_struct* p_timing) {
  return p_timing->countdown;
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
    util_bail("out of timer ids");
  }

  p_timer = &p_timing->timers[i];

  p_timer->p_callback = p_callback;
  p_timer->p_object = p_object;
  p_timer->value = INT64_MAX;
  p_timer->ticking = 0;
  p_timer->firing = 1;
  p_timer->p_expiry_prev = NULL;
  p_timer->p_expiry_next = NULL;

  p_timing->num_timers++;

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

  while ((p_iter != NULL) && (p_timer->value >= p_iter->value)) {
    assert((p_prev == NULL) || (p_iter->value >= p_prev->value));
    if (p_iter->p_expiry_next) {
      assert(p_iter->p_expiry_next->p_expiry_prev == p_iter);
    }
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

static void
timing_insert_ticking_timer(struct timing_struct* p_timing,
                            struct timer_struct* p_timer) {
  struct timer_struct* p_old_head = p_timing->p_ticking_head;

  assert(p_timer->p_ticking_prev == NULL);
  assert(p_timer->p_ticking_next == NULL);

  p_timing->p_ticking_head = p_timer;
  p_timer->p_ticking_next = p_old_head;
  if (p_old_head) {
    p_old_head->p_ticking_prev = p_timer;
  }
}

static void
timing_remove_ticking_timer(struct timing_struct* p_timing,
                            struct timer_struct* p_timer) {
  if (p_timer->p_ticking_prev) {
    p_timer->p_ticking_prev->p_ticking_next = p_timer->p_ticking_next;
  } else {
    assert(p_timer == p_timing->p_ticking_head);
    p_timing->p_ticking_head = p_timer->p_ticking_next;
  }
  if (p_timer->p_ticking_next) {
    p_timer->p_ticking_next->p_ticking_prev = p_timer->p_ticking_prev;
  }

  p_timer->p_ticking_prev = NULL;
  p_timer->p_ticking_next = NULL;
}

static int64_t
timing_start_timer_with_internal_value(struct timing_struct* p_timing,
                                       struct timer_struct* p_timer,
                                       int64_t value) {
  assert(p_timer->p_callback != NULL);
  assert(!p_timer->ticking);

  value += timing_get_countdown_adjustment(p_timing);

  p_timer->value = value;
  p_timer->ticking = 1;

  timing_insert_ticking_timer(p_timing, p_timer);
  if (p_timer->firing) {
    timing_insert_expiring_timer(p_timing, p_timer);
  }

  return timing_update_counts(p_timing);
}

int64_t
timing_start_timer(struct timing_struct* p_timing, uint32_t id) {
  struct timer_struct* p_timer;

  assert(id < k_timing_num_timers);
  p_timer = &p_timing->timers[id];
  return timing_start_timer_with_internal_value(p_timing,
                                                p_timer,
                                                p_timer->value);
}

int64_t
timing_start_timer_with_value(struct timing_struct* p_timing,
                              uint32_t id,
                              int64_t time) {
  struct timer_struct* p_timer;

  assert(id < k_timing_num_timers);

  p_timer = &p_timing->timers[id];

  time *= p_timing->scale_factor;

  return timing_start_timer_with_internal_value(p_timing, p_timer, time);
}

int64_t
timing_stop_timer(struct timing_struct* p_timing, uint32_t id) {
  struct timer_struct* p_timer;

  assert(id < k_timing_num_timers);

  p_timer = &p_timing->timers[id];
  assert(p_timer->p_callback != NULL);
  assert(p_timer->ticking);

  p_timer->ticking = 0;

  timing_remove_ticking_timer(p_timing, p_timer);
  if (p_timer->firing) {
    timing_remove_expiring_timer(p_timing, p_timer);
  }

  /* While the timer is not ticking, store the timer value directly. This
   * avoids having to update it while the countdown ticks.
   */
  p_timer->value -= timing_get_countdown_adjustment(p_timing);

  return timing_update_counts(p_timing);
}

int
timing_timer_is_running(struct timing_struct* p_timing, uint32_t id) {
  assert(id < k_timing_num_timers);

  return p_timing->timers[id].ticking;
}

int64_t
timing_get_timer_value(struct timing_struct* p_timing, uint32_t id) {
  struct timer_struct* p_timer;
  int64_t ret;

  assert(id < k_timing_num_timers);

  p_timer = &p_timing->timers[id];
  ret = p_timer->value;
  if (p_timer->ticking) {
    ret -= timing_get_countdown_adjustment(p_timing);
  }
  ret /= p_timing->scale_factor;
  return ret;
}

int64_t
timing_set_timer_value(struct timing_struct* p_timing,
                       uint32_t id,
                       int64_t time) {
  struct timer_struct* p_timer;

  assert(id < k_timing_num_timers);

  p_timer = &p_timing->timers[id];
  assert(p_timer->p_callback != NULL);

  time *= p_timing->scale_factor;
  if (p_timer->ticking) {
    time += timing_get_countdown_adjustment(p_timing);
  }

  p_timer->value = time;

  if (p_timer->ticking && p_timer->firing) {
    timing_remove_expiring_timer(p_timing, p_timer);
    timing_insert_expiring_timer(p_timing, p_timer);
  }

  return timing_update_counts(p_timing);
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

  return timing_update_counts(p_timing);
}

int
timing_get_firing(struct timing_struct* p_timing, uint32_t id) {
  assert(id < k_timing_num_timers);

  return p_timing->timers[id].firing;
}

int64_t
timing_set_firing(struct timing_struct* p_timing, uint32_t id, int firing) {
  struct timer_struct* p_timer;

  int firing_changed = 0;

  assert(id < k_timing_num_timers);

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

  return timing_update_counts(p_timing);
}

static uint64_t
timing_do_advance_time(struct timing_struct* p_timing, uint64_t delta) {
  struct timer_struct* p_timer;

  delta += timing_get_countdown_adjustment(p_timing);

  /* Pass 1: update all timers with their correct new value. */
  p_timer = p_timing->p_ticking_head;
  while (p_timer != NULL) {
    assert(p_timer->ticking);
    if (p_timer->p_expiry_next) {
      assert(p_timer->p_expiry_next->p_expiry_prev == p_timer);
    }
    if (p_timer->p_ticking_next) {
      assert(p_timer->p_ticking_next->p_ticking_prev == p_timer);
    }

    p_timer->value -= delta;

    p_timer = p_timer->p_ticking_next;
  }

  /* Clear the countdown adjustment. */
  p_timing->next_timer_expiry = 0;
  p_timing->countdown = 0;

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

  return timing_update_counts(p_timing);
}

int64_t
timing_advance_time(struct timing_struct* p_timing, int64_t countdown_target) {
  uint64_t countdown;
  int64_t delta;

  countdown = p_timing->countdown;
  delta = (countdown - countdown_target);

  assert(delta >= 0);

  while (delta) {
    /* Common case: delta is less than the amount to the next event. Get out
     * quickly.
     */
    if ((uint64_t) delta < countdown) {
      countdown -= delta;
      p_timing->countdown = countdown;
      p_timing->total_timer_ticks += delta;

      return countdown;
    }

    p_timing->total_timer_ticks += countdown;
    delta -= countdown;
    countdown = timing_do_advance_time(p_timing, countdown);
  }

  return countdown;
}

int64_t
timing_advance_time_delta(struct timing_struct* p_timing, uint64_t delta) {
  uint64_t countdown = p_timing->countdown;
  countdown -= delta;
  return timing_advance_time(p_timing, countdown);
}

#include "test-timing.c"
