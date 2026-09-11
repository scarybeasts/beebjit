#include "os_sound.h"

#include "util.h"

static const uint32_t k_os_sound_default_rate = 48000;
static uint32_t k_os_sound_default_buffer_size = 2048;
static const uint32_t k_os_sound_default_num_periods = 4;

struct os_sound_struct {
  struct util_file* p_output;
  char* p_device_name;
  uint32_t sample_rate;
  uint32_t buffer_size;
  uint32_t num_periods;
  uint32_t period_size;
};

uint32_t
os_sound_get_default_sample_rate(void) {
  return k_os_sound_default_rate;
}

uint32_t
os_sound_get_default_buffer_size(void) {
  return k_os_sound_default_buffer_size;
}

uint32_t
os_sound_get_default_num_periods(void) {
  return k_os_sound_default_num_periods;
}

struct os_sound_struct*
os_sound_create(char* p_device_name,
                uint32_t sample_rate,
                uint32_t buffer_size,
                uint32_t num_periods) {
  struct os_sound_struct* p_driver =
      util_mallocz(sizeof(struct os_sound_struct));
  if (p_device_name == NULL) {
    p_driver->p_device_name = NULL;
  } else {
    p_driver->p_device_name = strdup(p_device_name);
  }
  p_driver->sample_rate = sample_rate;
  p_driver->buffer_size = buffer_size;
  p_driver->num_periods = num_periods;
  p_driver->period_size = (p_driver->buffer_size / p_driver->num_periods);
  return p_driver;
}

void
os_sound_destroy(struct os_sound_struct* p_driver) {
  if (p_driver->p_output != NULL) {
    util_file_close(p_driver->p_output);
    p_driver->p_output = NULL;
  }
}

int
os_sound_init(struct os_sound_struct* p_driver) {
  if (p_driver->p_device_name == NULL) {
    return -1;
  }
  p_driver->p_output = util_file_try_open(p_driver->p_device_name, 1, 1);
  if (p_driver->p_output == NULL) {
    log_do_log(k_log_audio, k_log_error, "open failed");
    return -1;
  }
  return 0;
}

uint32_t
os_sound_get_sample_rate(struct os_sound_struct* p_driver) {
  return p_driver->sample_rate;
}

uint32_t
os_sound_get_buffer_size(struct os_sound_struct* p_driver) {
  return p_driver->buffer_size;
}

uint32_t
os_sound_get_period_size(struct os_sound_struct* p_driver) {
  return p_driver->period_size;
}

void
os_sound_write(struct os_sound_struct* p_driver,
               int16_t* p_frames,
               uint32_t num_frames) {
  util_file_write(p_driver->p_output, p_frames, num_frames * sizeof(int16_t));
}
