#ifndef BEEBJIT_IBM_DISC_FORMAT_H
#define BEEBJIT_IBM_DISC_FORMAT_H

enum {
  k_ibm_disc_mark_clock_pattern = 0xC7,
  k_ibm_disc_id_mark_data_pattern = 0xFE,
  k_ibm_disc_data_mark_data_pattern = 0xFB,
  k_ibm_disc_deleted_data_mark_data_pattern = 0xF8,
};

#endif /* BEEBJIT_IBM_DISC_FORMAT_H */
