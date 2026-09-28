/* Appends at the end of sound.c */

#include "test.h"

struct bbc_options g_p_options;

void
sound_test() {
  struct bbc_options options;
  struct sound_struct* p_sound;
  struct timing_struct* p_timing = timing_create(1);
  int16_t frames[16];

  (void) memset(&options, '\0', sizeof(options));
  options.p_opt_flags = "";
  options.p_log_flags = "";
  options.accurate = 1;

  p_sound = sound_create(1, p_timing, &options);

  sound_power_on_reset(p_sound);

  /* Terrible hack. */
  p_sound->p_driver = (void*) 1;
  p_sound->p_sn_frames = &frames[0];
  p_sound->sn_frames_per_driver_buffer_size = 16;

  test_expect_u32(0, p_sound->latched_address);
  test_expect_u32(0, p_sound->volume[3]);

  /* VIA effects arrive at the mid-cycle of the 1MHz. */
  (void) timing_advance_time_delta(p_timing, 1);
  test_expect_u32(1, p_sound->is_write_enabled);
  test_expect_u32(0, p_sound->pending_write_enable);
  test_expect_u32(0, p_sound->write_cycle_position);
  test_expect_u32(0, p_sound->sn_frames_filled);

  /* Disable write enable. */
  sound_sn_IC32_updated(p_sound, 1);
  test_expect_u32(1, p_sound->is_write_enabled);
  test_expect_u32(1, p_sound->pending_write_enable);
  (void) timing_advance_time_delta(p_timing, 1);
  sound_advance_sn_timing(p_sound);
  test_expect_u32(0, p_sound->is_write_enabled);
  test_expect_u32(0, p_sound->pending_write_enable);
  test_expect_u32(0, p_sound->sn_frames_filled);

  (void) timing_advance_time_delta(p_timing, 6);
  /* Total: 8 ticks. */
  sound_advance_sn_timing(p_sound);
  test_expect_u32(1, p_sound->sn_frames_filled);

  /* Set a bus value. */
  (void) timing_advance_time_delta(p_timing, 1);
  sound_sn_set_bus_value(p_sound, 0xFF);
  (void) timing_advance_time_delta(p_timing, 7);
  /* Total: 16 ticks. */

  /* Enable write enable. */
  (void) timing_advance_time_delta(p_timing, 1);
  sound_sn_IC32_updated(p_sound, 0);
  test_expect_u32(0, p_sound->is_write_enabled);
  test_expect_u32(1, p_sound->pending_write_enable);
  (void) timing_advance_time_delta(p_timing, 7);
  /* Total: 24 ticks. */
  /* Disable write enable. */
  (void) timing_advance_time_delta(p_timing, 1);
  sound_sn_IC32_updated(p_sound, 1);
  (void) timing_advance_time_delta(p_timing, 7);
  /* Total: 32 ticks. */
  sound_advance_sn_timing(p_sound);
  test_expect_u32(0, p_sound->is_write_enabled);
  test_expect_u32(0, p_sound->pending_write_enable);
  /* Catches an address latch but not a register write (volume change). */
  test_expect_u32(0x70, p_sound->latched_address);
  test_expect_u32(0, p_sound->volume[3]);

  /* More terrible hack. */
  p_sound->p_driver = NULL;
  p_sound->p_sn_frames = NULL;

  sound_destroy(p_sound);
}
