#ifndef BEEBJIT_DISC_H
#define BEEBJIT_DISC_H

#include <stddef.h>
#include <stdint.h>

struct disc_struct;

struct bbc_options;
struct timing_struct;
struct util_file;

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
               int is_mutable,
               int convert_to_hfe);
struct util_file* disc_get_file(struct disc_struct* p_disc);
uint8_t* disc_allocate_format_metadata(struct disc_struct* p_disc,
                                       size_t num_bytes);
void disc_set_is_double_sided(struct disc_struct* p_disc, int is_double_sided);

int disc_is_double_sided(struct disc_struct* p_disc);
int disc_is_spinning(struct disc_struct* p_disc);
int disc_is_write_protected(struct disc_struct* p_disc);
int disc_is_upper_side(struct disc_struct* p_disc);
uint32_t disc_get_track(struct disc_struct* p_disc);
int disc_is_index_pulse(struct disc_struct* p_disc);
uint32_t disc_get_head_position(struct disc_struct* p_disc);

int disc_is_track_dirty(struct disc_struct* p_disc);
void disc_set_track_dirty(struct disc_struct* p_disc, int is_dirty);
uint8_t* disc_get_format_metadata(struct disc_struct* p_disc);
uint8_t* disc_get_raw_track_data(struct disc_struct* p_disc);
uint8_t* disc_get_raw_track_clocks(struct disc_struct* p_disc);

void disc_start_spinning(struct disc_struct* p_disc);
void disc_stop_spinning(struct disc_struct* p_disc);
void disc_select_side(struct disc_struct* p_disc, int is_upper_side);
void disc_select_track(struct disc_struct* p_disc, uint32_t track);
void disc_seek_track(struct disc_struct* p_disc, int32_t delta);
void disc_write_byte(struct disc_struct* p_disc, uint8_t data, uint8_t clocks);

void disc_build_track(struct disc_struct* p_disc,
                      int is_side_upper,
                      uint32_t track);
void disc_build_reset_crc(struct disc_struct* p_disc);
void disc_build_append_single_with_clocks(struct disc_struct* p_disc,
                                          uint8_t data,
                                          uint8_t clocks);
void disc_build_append_single(struct disc_struct* p_disc, uint8_t data);
void disc_build_append_repeat(struct disc_struct* p_disc,
                              uint8_t data,
                              size_t num);
void disc_build_append_repeat_with_clocks(struct disc_struct* p_disc,
                                          uint8_t data,
                                          uint8_t clocks,
                                          size_t num);
void disc_build_append_chunk(struct disc_struct* p_disc,
                             uint8_t* p_src,
                             size_t num);
void disc_build_append_crc(struct disc_struct* p_disc);
void disc_build_append_bad_crc(struct disc_struct* p_disc);

#endif /* BEEBJIT_DISC_H */
