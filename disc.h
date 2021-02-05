#ifndef BEEBJIT_DISC_H
#define BEEBJIT_DISC_H

#include <stddef.h>
#include <stdint.h>

struct disc_struct;

struct bbc_options;
struct timing_struct;
struct util_file;

enum {
  k_disc_max_bytes_per_track = (256 * 13),
};

struct disc_struct* disc_create(const char* p_filename,
                                int is_writeable,
                                int is_mutable,
                                int do_convert_to_hfe,
                                int do_convert_to_ssd,
                                int do_convert_to_adl,
                                struct bbc_options* p_options);
struct disc_struct* disc_create_from_raw(const char* p_file_name,
                                         const char* p_raw_spec);
void disc_destroy(struct disc_struct* p_disc);

int disc_is_double_sided(struct disc_struct* p_disc);
uint32_t disc_get_num_tracks_used(struct disc_struct* p_disc);

const char* disc_get_file_name(struct disc_struct* p_disc);
struct util_file* disc_get_file(struct disc_struct* p_disc);
uint8_t* disc_allocate_format_metadata(struct disc_struct* p_disc,
                                       size_t num_bytes);
void disc_set_track_length(struct disc_struct* p_disc,
                           int is_side_upper,
                           uint32_t track,
                           uint32_t length);

int disc_is_double_sided(struct disc_struct* p_disc);
int disc_is_write_protected(struct disc_struct* p_disc);

uint8_t* disc_get_format_metadata(struct disc_struct* p_disc);
uint32_t disc_get_track_length(struct disc_struct* p_disc,
                               int is_side_upper,
                               uint32_t track);
uint32_t disc_read_pulses(struct disc_struct* p_disc,
                          int is_side_upper,
                          uint32_t track,
                          uint32_t pos);
uint32_t* disc_get_raw_pulses_buffer(struct disc_struct* p_disc,
                                     int is_side_upper,
                                     uint32_t track);

void disc_write_pulses(struct disc_struct* p_disc,
                       int is_side_upper,
                       uint32_t track,
                       uint32_t pos,
                       uint32_t pulses);
void disc_dirty_and_flush(struct disc_struct* p_disc,
                          int is_side_upper,
                          uint32_t track);
void disc_flush_writes(struct disc_struct* p_disc);

void disc_build_track(struct disc_struct* p_disc,
                      int is_side_upper,
                      uint32_t track);
void disc_build_set_track_length(struct disc_struct* p_disc);
void disc_build_reset_crc(struct disc_struct* p_disc);

/* FM */
void disc_build_append_fm_data_and_clocks(struct disc_struct* p_disc,
                                          uint8_t data,
                                          uint8_t clocks);
void disc_build_append_fm_byte(struct disc_struct* p_disc, uint8_t data);
void disc_build_append_repeat_fm_byte(struct disc_struct* p_disc,
                                      uint8_t data,
                                      size_t num);
void disc_build_append_repeat_fm_byte_with_clocks(struct disc_struct* p_disc,
                                                  uint8_t data,
                                                  uint8_t clocks,
                                                  size_t num);
void disc_build_append_fm_chunk(struct disc_struct* p_disc,
                                uint8_t* p_src,
                                size_t num);
/* MFM */
void disc_build_append_mfm_byte(struct disc_struct* p_disc, uint8_t data);
void disc_build_append_repeat_mfm_byte(struct disc_struct* p_disc,
                                       uint8_t data,
                                       uint32_t count);
void disc_build_append_mfm_3x_A1_sync(struct disc_struct* p_disc);
void disc_build_append_mfm_chunk(struct disc_struct* p_disc,
                                 uint8_t* p_src,
                                 uint32_t count);
void disc_build_fill_mfm_byte(struct disc_struct* p_disc, uint8_t data);

void disc_build_append_crc(struct disc_struct* p_disc, int is_mfm);
void disc_build_append_bad_crc(struct disc_struct* p_disc);
void disc_build_fill_fm_byte(struct disc_struct* p_disc, uint8_t data);

/* Raw pulses. */
void disc_build_track_from_pulses(struct disc_struct* p_disc,
                                  uint32_t rev,
                                  int is_side_upper,
                                  uint32_t track,
                                  float* p_pulse_deltas,
                                  uint32_t num_pulses);

#endif /* BEEBJIT_DISC_H */
