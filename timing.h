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
void timing_start_timer(struct timing_struct* p_timing,
                        size_t id,
                        int64_t time);
void timing_stop_timer(struct timing_struct* p_timing, size_t id);
int64_t timing_increase_timer(struct timing_struct* p_timing,
                              size_t id,
                              int64_t time);

int64_t timing_next_timer(struct timing_struct* p_timing);
int64_t timing_advance(struct timing_struct* p_timing, int64_t time);

/* Legacy APIs. */
void timing_set_sync_tick_callback(struct timing_struct* p_timing,
                                   void* p_callback,
                                   void* p_object);
void timing_do_sync_tick_callback(struct timing_struct* p_timing);

#endif /* BEEBJIT_TIMING_H */
