#ifndef BEEBJIT_OS_SOUND_H
#define BEEBJIT_OS_SOUND_H

#include <stdint.h>

struct os_sound_struct;

uint32_t os_sound_get_default_sample_rate(void);
uint32_t os_sound_get_default_buffer_size(void);
uint32_t os_sound_get_default_num_periods(void);

struct os_sound_struct* os_sound_create(char* p_device_name,
                                        uint32_t sample_rate,
                                        uint32_t buffer_size,
                                        uint32_t num_periods);
void os_sound_destroy(struct os_sound_struct* p_driver);

int os_sound_init(struct os_sound_struct* p_driver);

uint32_t os_sound_get_sample_rate(struct os_sound_struct* p_driver);
uint32_t os_sound_get_buffer_size(struct os_sound_struct* p_driver);
uint32_t os_sound_get_period_size(struct os_sound_struct* p_driver);

void os_sound_write(struct os_sound_struct* p_driver,
                    int16_t* p_frames,
                    uint32_t num_frames);

#endif /* BEEBJIT_OS_SOUND_H */
