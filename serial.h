#ifndef BEEBJIT_SERIAL_H
#define BEEBJIT_SERIAL_H

#include <stdint.h>

struct serial_struct;

struct bbc_options;
struct state_6502;
struct tape_struct;

struct serial_struct* serial_create(struct state_6502* p_state_6502,
                                    int fasttape_flag,
                                    struct bbc_options* p_options);
void serial_destroy(struct serial_struct* p_serial);

void serial_set_fast_mode_callback(struct serial_struct* p_serial,
                                   void (*set_fast_mode_callback)(void* p,
                                                                  int fast),
                                   void* p_set_fast_mode_object);

void serial_set_io_handles(struct serial_struct* p_serial,
                           intptr_t handle_input,
                           intptr_t handle_output);
void serial_set_tape(struct serial_struct* p_serial,
                     struct tape_struct* p_tape);

void serial_power_on_reset(struct serial_struct* p_serial);

void serial_tick(struct serial_struct* p_serial);

uint8_t serial_acia_read(struct serial_struct* p_serial, uint8_t reg);
void serial_acia_write(struct serial_struct* p_serial,
                       uint8_t reg,
                       uint8_t val);
uint8_t serial_ula_read(struct serial_struct* p_serial);
void serial_ula_write(struct serial_struct* p_serial, uint8_t val);

#endif /* BEEBJIT_SERIAL_H */
