#ifndef BEEBJIT_IBM_DISC_FORMAT_H
#define BEEBJIT_IBM_DISC_FORMAT_H

#include <stdint.h>

enum {
  k_ibm_disc_bytes_per_track = 3125,
  k_ibm_disc_tracks_per_disc = 84,

  k_ibm_disc_mark_clock_pattern = 0xC7,
  k_ibm_disc_id_mark_data_pattern = 0xFE,
  k_ibm_disc_data_mark_data_pattern = 0xFB,
  k_ibm_disc_deleted_data_mark_data_pattern = 0xF8,

  k_ibm_disc_mfm_a1_sync = 0x4489,
  k_ibm_disc_mfm_c2_sync = 0x5224,

  k_ibm_disc_std_sync_00s = 6,
  k_ibm_disc_std_gap1_FFs = 16,
  k_ibm_disc_std_gap2_FFs = 11,
  k_ibm_disc_std_10_sector_gap3_FFs = 21,
};

uint16_t ibm_disc_format_crc_init(int is_mfm);
uint16_t ibm_disc_format_crc_add_byte(uint16_t crc, uint8_t byte);

uint32_t ibm_disc_format_fm_to_2us_pulses(uint8_t clocks, uint8_t data);
void ibm_disc_format_2us_pulses_to_fm(uint8_t* p_clocks,
                                      uint8_t* p_data,
                                      int* p_is_iffy_pulse,
                                      uint32_t pulses);

uint16_t ibm_disc_format_mfm_to_2us_pulses(int* p_last_mfm_bit, uint8_t byte);
uint8_t ibm_disc_format_2us_pulses_to_mfm(uint16_t pulses);

int ibm_disc_format_check_pulse(float pulse_us, int is_mfm);

#endif /* BEEBJIT_IBM_DISC_FORMAT_H */
