#ifndef BEEBJIT_TIMING_H
#define BEEBJIT_TIMING_H

#include <stddef.h>
#include <stdint.h>

struct timing_struct;

struct timing_struct* timing_create(uint32_t tick_rate);
void timing_destroy(struct timing_struct* p_timing);

uint64_t timing_get_total_timer_ticks(struct timing_struct* p_timing);
uint32_t timing_get_tick_rate(struct timing_struct* p_timing);

size_t timing_register_timer(struct timing_struct* p_timing,
                             void* p_callback,
                             void* p_object);
int64_t timing_start_timer(struct timing_struct* p_timing,
                           size_t id,
                           int64_t time);
int64_t timing_stop_timer(struct timing_struct* p_timing, size_t id);
int timing_timer_is_running(struct timing_struct* p_timing, size_t id);
int64_t timing_get_timer_value(struct timing_struct* p_timing, size_t id);
int64_t timing_set_timer_value(struct timing_struct* p_timing,
                               size_t id,
                               int64_t value);
int64_t timing_adjust_timer_value(struct timing_struct* p_timing,
                                  int64_t* p_new_value,
                                  size_t id,
                                  int64_t delta);
int timing_get_firing(struct timing_struct* p_timing, size_t id);
void timing_set_firing(struct timing_struct* p_timing, size_t id, int firing);

int64_t timing_get_countdown(struct timing_struct* p_timing);
int64_t timing_advance_time(struct timing_struct* p_timing, int64_t countdown);

#endif /* BEEBJIT_TIMING_H */
