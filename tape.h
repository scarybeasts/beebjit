#ifndef BEEBJIT_TAPE_H
#define BEEBJIT_TAPE_H

#include <stdint.h>

struct tape_struct;

struct bbc_options;
struct serial_ula_struct;
struct timing_struct;

enum {
  k_tape_max_file_size = (1024 * 1024 * 8),
};

enum {
  k_tape_bit_0 = 0,
  k_tape_bit_1 = 1,
  k_tape_bit_silence = -1,
};

struct tape_struct* tape_create(struct timing_struct* p_timing,
                                struct bbc_options* p_options);
void tape_destroy(struct tape_struct* p_tape);

void tape_set_serial_ula(struct tape_struct* p_tape,
                         struct serial_ula_struct* p_serial_ula);

void tape_power_on_reset(struct tape_struct* p_tape);

void tape_add_tape(struct tape_struct* p_tape, const char* p_filename);
void tape_cycle_tape(struct tape_struct* p_tape);

int tape_is_playing(struct tape_struct* p_tape);

void tape_play(struct tape_struct* p_tape);
void tape_stop(struct tape_struct* p_tape);
void tape_rewind(struct tape_struct* p_tape);

void tape_add_bit(struct tape_struct* p_tape, int8_t bit);
void tape_add_bits(struct tape_struct* p_tape, int8_t bit, uint32_t num_bits);
/* Convenience to add a standard 8N1 format byte. */
void tape_add_byte(struct tape_struct* p_tape, uint8_t byte);

#endif /* BEEBJIT_TAPE_H */
