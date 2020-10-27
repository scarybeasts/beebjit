#ifndef BEEBJIT_DISC_DRIVE_H
#define BEEBJIT_DISC_DRIVE_H

#include <stdint.h>

struct disc_drive_struct;

struct bbc_options;
struct disc_struct;
struct timing_struct;

struct disc_drive_struct* disc_drive_create(uint32_t id,
                                            struct timing_struct* p_timing,
                                            struct bbc_options* p_options);
void disc_drive_destroy(struct disc_drive_struct* p_drive);
void disc_drive_set_pulses_callback(struct disc_drive_struct* p_drive,
                                    void (*p_pulses_callback)(void* p,
                                                              uint32_t pulses,
                                                              uint32_t count),
                                    void* p_pulses_callback_object);
/* Normally, 64us worth of pulses (32x 2us each) are delivered, suitable for FM.
 * This selects 32us worth (16x 2us each), suitable for MFM.
 */
void disc_drive_set_32us_mode(struct disc_drive_struct* p_drive, int on);

void disc_drive_power_on_reset(struct disc_drive_struct* p_drive);

void disc_drive_add_disc(struct disc_drive_struct* p_drive,
                         struct disc_struct* p_disc);
void disc_drive_cycle_disc(struct disc_drive_struct* p_drive);

struct disc_struct* disc_drive_get_disc(struct disc_drive_struct* p_drive);
int disc_drive_is_spinning(struct disc_drive_struct* p_drive);
int disc_drive_is_upper_side(struct disc_drive_struct* p_drive);
uint32_t disc_drive_get_track(struct disc_drive_struct* p_drive);
int disc_drive_is_index_pulse(struct disc_drive_struct* p_drive);
uint32_t disc_drive_get_head_position(struct disc_drive_struct* p_drive);
int disc_drive_is_write_protect(struct disc_drive_struct* p_drive);

uint32_t disc_drive_get_quasi_random_pulses(struct disc_drive_struct* p_drive);

void disc_drive_start_spinning(struct disc_drive_struct* p_drive);
void disc_drive_stop_spinning(struct disc_drive_struct* p_drive);
void disc_drive_select_side(struct disc_drive_struct* p_drive,
                            int is_upper_side);
void disc_drive_select_track(struct disc_drive_struct* p_drive, int32_t track);
void disc_drive_seek_track(struct disc_drive_struct* p_drive, int32_t delta);
void disc_drive_write_pulses(struct disc_drive_struct* p_drive,
                             uint32_t pulses);

#endif /* BEEBJIT_DISC_DRIVE_H */
