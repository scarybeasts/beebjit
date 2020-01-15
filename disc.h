#ifndef BEEBJIT_DISC_H
#define BEEBJIT_DISC_H

#include <stdint.h>

struct disc_struct;

struct timing_struct;

struct disc_struct* disc_create(struct timing_struct* p_timing,
                                void (*p_byte_callback)
                                    (void* p,
                                     uint8_t data,
                                     uint8_t clock,
                                     int is_index),
                                void* p_byte_callback_object);
void disc_destroy(struct disc_struct* p_disc);

void disc_load(struct disc_struct* p_disc, const char* p_filename);

void disc_start_spinning(struct disc_struct* p_disc);
void disc_stop_spinning(struct disc_struct* p_disc);
void disc_select_side(struct disc_struct* p_disc, int is_upper_side);
void disc_select_track(struct disc_struct* p_disc, uint32_t track);

#endif /* BEEBJIT_DISC_H */
