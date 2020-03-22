#include "os_sound.h"

struct os_sound_struct*
os_sound_create(char* p_device_name,
                uint32_t sample_rate,
                uint32_t buffer_size) {
  (void) p_device_name;
  (void) sample_rate;
  (void) buffer_size;
  return NULL;
}

void
os_sound_destroy(struct os_sound_struct* p_driver) {
  (void) p_driver;
}

int
os_sound_init(struct os_sound_struct* p_driver) {
  (void) p_driver;
  return -1;
}

uint32_t
os_sound_get_sample_rate(struct os_sound_struct* p_driver) {
  (void) p_driver;
  return 0;
}

uint32_t
os_sound_get_buffer_size(struct os_sound_struct* p_driver) {
  (void) p_driver;
  return 0;
}

uint32_t
os_sound_get_period_size(struct os_sound_struct* p_driver) {
  (void) p_driver;
  return 0;
}

uint32_t
os_sound_wait_for_frame_space(struct os_sound_struct* p_driver) {
  (void) p_driver;
  return 0;
}

uint32_t
os_sound_get_frame_space(struct os_sound_struct* p_driver) {
  (void) p_driver;
  return 0;
}

void
os_sound_write(struct os_sound_struct* p_driver,
               int16_t* p_frames,
               uint32_t num_frames) {
  (void) p_driver;
  (void) p_frames;
  (void) num_frames;
}
