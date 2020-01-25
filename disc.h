#ifndef BEEBJIT_DISC_H
#define BEEBJIT_DISC_H

#include <stdint.h>

struct disc_struct;

struct bbc_options;
struct timing_struct;

struct disc_struct* disc_create(struct timing_struct* p_timing,
                                void (*p_byte_callback)(void* p,
                                                        uint8_t data,
                                                        uint8_t clock),
                                void* p_byte_callback_object,
                                struct bbc_options* p_options);
void disc_destroy(struct disc_struct* p_disc);

void disc_load(struct disc_struct* p_disc,
               const char* p_filename,
               int is_writeable,
               int is_mutable);

int disc_is_spinning(struct disc_struct* p_disc);
int disc_is_write_protected(struct disc_struct* p_disc);
uint32_t disc_get_track(struct disc_struct* p_disc);
int disc_is_index_pulse(struct disc_struct* p_disc);
uint32_t disc_get_head_position(struct disc_struct* p_disc);

void disc_start_spinning(struct disc_struct* p_disc);
void disc_stop_spinning(struct disc_struct* p_disc);
void disc_select_side(struct disc_struct* p_disc, int is_upper_side);
void disc_select_track(struct disc_struct* p_disc, uint32_t track);
void disc_seek_track(struct disc_struct* p_disc, int32_t delta);
void disc_write_byte(struct disc_struct* p_disc, uint8_t data, uint8_t clocks);

#endif /* BEEBJIT_DISC_H */
