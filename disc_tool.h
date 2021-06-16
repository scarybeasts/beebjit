#ifndef BEEBJIT_DISC_TOOL_H
#define BEEBJIT_DISC_TOOL_H

#include <stdint.h>

enum {
  k_disc_tool_max_sector_length = 2048,
};

struct disc_tool_sector {
  int is_mfm;
  uint32_t bit_pos_header;
  uint32_t bit_pos_data;
  uint8_t header_bytes[6];
  uint16_t header_crc_on_disc;
  int is_deleted;
  uint32_t byte_length;
  uint16_t data_crc_on_disc;
  int has_header_crc_error;
  int has_data_crc_error;
};

struct disc_tool_struct;

struct disc_struct;

void disc_tool_log_summary(struct disc_struct* p_disc,
                           int log_crc_errors,
                           int log_protection,
                           int log_fingerprint,
                           int log_fingerprint_tracks,
                           int log_catalog,
                           int do_dump_sector_data);

struct disc_tool_struct* disc_tool_create();
void disc_tool_destroy(struct disc_tool_struct* p_tool);

uint32_t disc_tool_get_track(struct disc_tool_struct* p_tool);
uint32_t disc_tool_get_byte_pos(struct disc_tool_struct* p_tool);

void disc_tool_set_disc(struct disc_tool_struct* p_tool,
                        struct disc_struct* p_disc);
void disc_tool_set_is_side_upper(struct disc_tool_struct* p_tool,
                                 int is_side_upper);
void disc_tool_set_track(struct disc_tool_struct* p_tool, uint32_t track);
void disc_tool_set_byte_pos(struct disc_tool_struct* p_tool, uint32_t pos);

void disc_tool_read_fm_data(struct disc_tool_struct* p_tool,
                            uint8_t* p_clocks,
                            uint8_t* p_data,
                            int* p_is_iffy_pulse,
                            uint32_t len);
void disc_tool_write_fm_data(struct disc_tool_struct* p_tool,
                             uint8_t* p_clocks,
                             uint8_t* p_data,
                             uint32_t len);
void disc_tool_fill_fm_data(struct disc_tool_struct* p_tool, uint8_t data);
void disc_tool_read_mfm_data(struct disc_tool_struct* p_tool,
                             uint8_t* p_data,
                             uint32_t len);

void disc_tool_find_sectors(struct disc_tool_struct* p_tool);
struct disc_tool_sector* disc_tool_get_sectors(struct disc_tool_struct* p_tool,
                                               uint32_t* p_num_sectors);
void disc_tool_read_sector(struct disc_tool_struct* p_tool,
                           uint32_t* p_byte_length,
                           uint8_t* p_data,
                           uint32_t sector,
                           int do_include_marker);

#endif /* BEEBJIT_DISC_TOOL_H */
