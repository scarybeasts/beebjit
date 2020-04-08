#ifndef BEEBJIT_DISC_H
#define BEEBJIT_DISC_H

#include <stddef.h>
#include <stdint.h>

struct disc_struct;

struct bbc_options;
struct timing_struct;
struct util_file;

struct disc_struct* disc_create(const char* p_filename,
                                int is_writeable,
                                int is_mutable,
                                int convert_to_hfe,
                                struct bbc_options* p_options);
struct disc_struct* disc_create_from_raw(const char* p_file_name,
                                         const char* p_raw_spec);
void disc_destroy(struct disc_struct* p_disc);

const char* disc_get_file_name(struct disc_struct* p_disc);
struct util_file* disc_get_file(struct disc_struct* p_disc);
uint8_t* disc_allocate_format_metadata(struct disc_struct* p_disc,
                                       size_t num_bytes);
void disc_set_is_double_sided(struct disc_struct* p_disc, int is_double_sided);

int disc_is_double_sided(struct disc_struct* p_disc);
int disc_is_write_protected(struct disc_struct* p_disc);

uint8_t* disc_get_format_metadata(struct disc_struct* p_disc);
uint8_t* disc_get_raw_track_data(struct disc_struct* p_disc,
                                 int is_side_upper,
                                 uint32_t track);
uint8_t* disc_get_raw_track_clocks(struct disc_struct* p_disc,
                                   int is_side_upper,
                                   uint32_t track);

void disc_write_byte(struct disc_struct* p_disc,
                     int is_side_upper,
                     uint32_t track,
                     uint32_t pos,
                     uint8_t data,
                     uint8_t clocks);
void disc_flush_writes(struct disc_struct* p_disc);

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
void disc_build_fill(struct disc_struct* p_disc, uint8_t data);

#endif /* BEEBJIT_DISC_H */
