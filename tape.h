#ifndef BEEBJIT_TAPE_H
#define BEEBJIT_TAPE_H

#include <stdint.h>

struct tape_struct;

struct bbc_options;
struct serial_struct;
struct timing_struct;

struct tape_struct* tape_create(struct timing_struct* p_timing,
                                struct bbc_options* p_options);
void tape_destroy(struct tape_struct* p_tape);

void tape_set_status_callback(struct tape_struct* p_tape,
                              void (*p_status_callback)(void* p,
                                                        int carrier,
                                                        int32_t value),
                              void* p_status_callback_object);

void tape_power_on_reset(struct tape_struct* p_tape);

void tape_add_tape(struct tape_struct* p_tape, const char* p_filename);
void tape_cycle_tape(struct tape_struct* p_tape);

int tape_is_playing(struct tape_struct* p_tape);

void tape_play(struct tape_struct* p_tape);
void tape_stop(struct tape_struct* p_tape);
void tape_rewind(struct tape_struct* p_tape);

#endif /* BEEBJIT_TAPE_H */
