#ifndef BEEBJIT_SERIAL_H
#define BEEBJIT_SERIAL_H

#include <stdint.h>

struct serial_struct;

struct state_6502;
struct tape_struct;

struct serial_struct* serial_create(struct state_6502* p_state_6502);
void serial_destroy(struct serial_struct* p_serial);

void serial_set_io_handles(struct serial_struct* p_serial,
                           intptr_t handle_input,
                           intptr_t handle_output);
void serial_set_tape(struct serial_struct* p_serial,
                     struct tape_struct* p_tape);

void serial_tick(struct serial_struct* p_serial);

uint8_t serial_acia_read(struct serial_struct* p_serial, uint8_t reg);
void serial_acia_write(struct serial_struct* p_serial,
                       uint8_t reg,
                       uint8_t val);
uint8_t serial_ula_read(struct serial_struct* p_serial);
void serial_ula_write(struct serial_struct* p_serial, uint8_t val);

int serial_is_receive_empty(struct serial_struct* p_serial);
void serial_receive(struct serial_struct* p_serial, uint8_t val);

void serial_tape_value_callback(void* p, int32_t tape_value);

#endif /* BEEBJIT_SERIAL_H */
