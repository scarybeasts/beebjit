#include "os_sound.h"

#include "util.h"

uint32_t
os_sound_get_default_buffer_size(void) {
  util_bail("headless");
  return 0;
}

struct os_sound_struct*
os_sound_create(char* p_device_name,
                uint32_t sample_rate,
                uint32_t buffer_size,
                uint32_t num_periods) {
  (void) p_device_name;
  (void) sample_rate;
  (void) buffer_size;
  (void) num_periods;
  util_bail("headless");
  return NULL;
}

void
os_sound_destroy(struct os_sound_struct* p_driver) {
  (void) p_driver;
  util_bail("headless");
}

int
os_sound_init(struct os_sound_struct* p_driver) {
  (void) p_driver;
  util_bail("headless");
  return -1;
}

uint32_t
os_sound_get_sample_rate(struct os_sound_struct* p_driver) {
  (void) p_driver;
  util_bail("headless");
  return 0;
}

uint32_t
os_sound_get_buffer_size(struct os_sound_struct* p_driver) {
  (void) p_driver;
  util_bail("headless");
  return 0;
}

uint32_t
os_sound_get_period_size(struct os_sound_struct* p_driver) {
  (void) p_driver;
  util_bail("headless");
  return 0;
}

void
os_sound_write(struct os_sound_struct* p_driver,
               int16_t* p_frames,
               uint32_t num_frames) {
  (void) p_driver;
  (void) p_frames;
  (void) num_frames;
  util_bail("headless");
}
