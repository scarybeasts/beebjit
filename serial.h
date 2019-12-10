#ifndef BEEBJIT_SERIAL_H
#define BEEBJIT_SERIAL_H

#include <stdint.h>

struct serial_struct;

struct state_6502;

struct serial_struct* serial_create(struct state_6502* p_state_6502);
void serial_destroy(struct serial_struct* p_serial);

uint8_t serial_acia_read(struct serial_struct* p_serial, uint8_t reg);
void serial_acia_write(struct serial_struct* p_serial,
                       uint8_t reg,
                       uint8_t val);

int serial_receive_empty(struct serial_struct* p_serial);
void serial_receive(struct serial_struct* p_serial, uint8_t val);

#endif /* BEEBJIT_KEYBOARD_H */
