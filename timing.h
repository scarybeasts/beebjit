#ifndef BEEBJIT_TIMING_H
#define BEEBJIT_TIMING_H

#include <stddef.h>
#include <stdint.h>

struct timing_struct;

struct timing_struct* timing_create();
void timing_destroy(struct timing_struct* p_timing);

size_t timing_register_timer(struct timing_struct* p_timing,
                             void* p_callback,
                             void* p_object);
int64_t timing_start_timer(struct timing_struct* p_timing,
                           size_t id,
                           int64_t time);
int64_t timing_stop_timer(struct timing_struct* p_timing, size_t id);
int timing_timer_is_running(struct timing_struct* p_timing, size_t id);
int64_t timing_increase_timer(int64_t* p_new_value,
                              struct timing_struct* p_timing,
                              size_t id,
                              int64_t time);

int64_t timing_get_countdown(struct timing_struct* p_timing);
uint64_t timing_update_countdown(struct timing_struct* p_timing,
                                 int64_t countdown);
int64_t timing_trigger_callbacks(struct timing_struct* p_timing);

/* Legacy APIs. */
void timing_set_sync_tick_callback(struct timing_struct* p_timing,
                                   void* p_callback,
                                   void* p_object);
void timing_do_sync_tick_callback(struct timing_struct* p_timing);

#endif /* BEEBJIT_TIMING_H */
