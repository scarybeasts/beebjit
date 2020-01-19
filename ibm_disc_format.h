#ifndef BEEBJIT_IBM_DISC_FORMAT_H
#define BEEBJIT_IBM_DISC_FORMAT_H

#include <stdint.h>

enum {
  k_ibm_disc_mark_clock_pattern = 0xC7,
  k_ibm_disc_id_mark_data_pattern = 0xFE,
  k_ibm_disc_data_mark_data_pattern = 0xFB,
  k_ibm_disc_deleted_data_mark_data_pattern = 0xF8,
};

uint16_t ibm_disc_format_crc_init();
uint16_t ibm_disc_format_crc_add_byte(uint16_t crc, uint8_t byte);

#endif /* BEEBJIT_IBM_DISC_FORMAT_H */
