#include "ibm_disc_format.h"

/* These CRC routines inspired by:
 * https://github.com/mamedev/mame/blob/master/src/devices/machine/i8271.cpp
 * https://people.cs.umu.se/isak/snippets/crc-16.c
 */

/* This is the CCITT CRC 16, using the polynomial x^16 + x^12 + x^5 + 1. */
/* This has been checked against a freshly formatted standard track 0.
 * CRC for track 0 sector 0 ID header is 0xF1 0xD3.
 * CRC for freshly formatted sector (full of 0xE5) is 0xA4 0x0C.
 */
uint16_t
ibm_disc_format_crc_init() {
  return 0xFFFF;
}

uint16_t
ibm_disc_format_crc_add_byte(uint16_t crc, uint8_t byte) {
  uint32_t i;

  for (i = 0; i < 8; ++i) {
    int bit = (byte & 0x80);
    int bit_test = ((crc & 0x8000) ^ (bit << 8));
    crc <<= 1;
    if (bit_test) {
      crc ^= 0x1021;
    }
    byte <<= 1;
  }

  return crc;
}

uint32_t
ibm_disc_format_fm_to_2us_pulses(uint8_t clocks, uint8_t data) {
  uint32_t i;
  uint32_t ret = 0;

  for (i = 0; i < 8; ++i) {
    ret <<= 4;
    if (clocks & 0x80) {
      ret |= 0x04;
    }
    if (data & 0x80) {
      ret |= 0x01;
    }
    clocks <<= 1;
    data <<= 1;
  }

  return ret;
}

void
ibm_disc_format_2us_pulses_to_fm(uint8_t* p_clocks,
                                 uint8_t* p_data,
                                 uint32_t pulses) {
  uint32_t i;
  uint8_t clocks = 0;
  uint8_t data = 0;

  /* This downsamples 2us resolution pulses into 4us FM pulses, splitting clocks
   * from data.
   */
  for (i = 0; i < 8; ++i) {
    clocks <<= 1;
    data <<= 1;
    if (pulses & 0xC0000000) {
      clocks |= 1;
    }
    if (pulses & 0x30000000) {
      data |= 1;
    }
    pulses <<= 4;
  }

  *p_clocks = clocks;
  *p_data = data;
}
