#ifndef BEEBJIT_MC6850_H
#define BEEBJIT_MC6850_H

#include <stdint.h>

struct serial_struct;

struct bbc_options;
struct state_6502;
struct tape_struct;

struct serial_struct* serial_create(struct state_6502* p_state_6502,
                                    struct bbc_options* p_options);
void serial_destroy(struct serial_struct* p_serial);

void serial_set_transmit_ready_callback(
    struct serial_struct* p_serial,
    void (*p_transmit_ready_callback)(void* p),
    void* p_transmit_ready_object);

void serial_power_on_reset(struct serial_struct* p_serial);

uint8_t serial_acia_read(struct serial_struct* p_serial, uint8_t reg);
void serial_acia_write(struct serial_struct* p_serial,
                       uint8_t reg,
                       uint8_t val);

void serial_set_DCD(struct serial_struct* p_serial, int is_DCD);
void serial_set_CTS(struct serial_struct* p_serial, int is_CTS);
int serial_get_RTS(struct serial_struct* p_serial);
int serial_is_transmit_ready(struct serial_struct* p_serial);
void serial_receive(struct serial_struct* p_serial, uint8_t byte);
uint8_t serial_transmit(struct serial_struct* p_serial);

#endif /* BEEBJIT_MC6850_H */
