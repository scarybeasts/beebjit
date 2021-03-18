#ifndef BEEBJIT_SOUND_H
#define BEEBJIT_SOUND_H

#include <stdint.h>

struct bbc_options;
struct os_sound_struct;
struct timing_struct;

struct sound_struct;

struct sound_struct* sound_create(int synchronous,
                                  struct timing_struct* p_timing,
                                  struct bbc_options* p_options);
void sound_destroy(struct sound_struct* p_sound);

void sound_set_driver(struct sound_struct* p_sound,
                      struct os_sound_struct* p_driver);
void sound_start_playing(struct sound_struct* p_sound);

void sound_power_on_reset(struct sound_struct* p_sound);

int sound_is_active(struct sound_struct* p_sound);
int sound_is_synchronous(struct sound_struct* p_sound);
void sound_tick(struct sound_struct* p_sound);

void sound_get_state(struct sound_struct* p_sound,
                     uint8_t* p_volumes,
                     uint16_t* p_periods,
                     uint16_t* p_counters,
                     uint8_t* p_outputs,
                     uint8_t* p_last_channel,
                     int* p_noise_type,
                     uint8_t* p_noise_frequency,
                     uint16_t* p_noise_rng);
void sound_set_state(struct sound_struct* p_sound,
                     uint8_t* p_volumes,
                     uint16_t* p_periods,
                     uint16_t* p_counters,
                     uint8_t* p_outputs,
                     uint8_t last_channel,
                     int noise_type,
                     uint8_t noise_frequency,
                     uint16_t noise_rng);

void sound_sn_write(struct sound_struct* p_sound, uint8_t data);

#endif /* BEEBJIT_SOUND_H */
