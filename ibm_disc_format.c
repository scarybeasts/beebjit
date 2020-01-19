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
