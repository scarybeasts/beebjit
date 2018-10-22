#include "sound.h"

#include <stdio.h>

#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_sound_num_channels = 4,
};

struct sound_struct {
  int write_status;
  unsigned char volume[k_sound_num_channels];
  uint16_t frequency[k_sound_num_channels];
  /* 0 - low, 1 - medium, 2 - high, 3 -- use tone generator 1. */
  int noise_frequency;
  /* 1 is white, 0 is periodic. */
  int noise_type;
};

struct sound_struct*
sound_create() {
  struct sound_struct* p_sound = malloc(sizeof(struct sound_struct));
  if (p_sound == NULL) {
    errx(1, "couldn't allocate sound_struct");
  }
  (void) memset(p_sound, '\0', sizeof(struct sound_struct));

  return p_sound;
}

void
sound_destroy(struct sound_struct* p_sound) {
  free(p_sound);
}

void
sound_apply_write_bit_and_data(struct sound_struct* p_sound,
                               int write,
                               unsigned char data) {
  int old_write_status = p_sound->write_status;
  p_sound->write_status = write;
  if (p_sound->write_status == 0 || old_write_status == 1) {
    return;
  }
printf("data: %x\n", data);
}
