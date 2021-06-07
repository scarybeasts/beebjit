#ifndef BEEBJIT_MC6850_H
#define BEEBJIT_MC6850_H

#include <stdint.h>

struct mc6850_struct;

struct bbc_options;
struct state_6502;
struct tape_struct;

struct mc6850_struct* mc6850_create(struct state_6502* p_state_6502,
                                    struct bbc_options* p_options);
void mc6850_destroy(struct mc6850_struct* p_serial);

void mc6850_set_transmit_ready_callback(
    struct mc6850_struct* p_serial,
    void (*p_transmit_ready_callback)(void* p),
    void* p_transmit_ready_object);

void mc6850_power_on_reset(struct mc6850_struct* p_serial);

uint8_t mc6850_read(struct mc6850_struct* p_serial, uint8_t reg);
void mc6850_write(struct mc6850_struct* p_serial, uint8_t reg, uint8_t val);

void mc6850_set_DCD(struct mc6850_struct* p_serial, int is_DCD);
void mc6850_set_CTS(struct mc6850_struct* p_serial, int is_CTS);
int mc6850_get_RTS(struct mc6850_struct* p_serial);
int mc6850_is_transmit_ready(struct mc6850_struct* p_serial);

void mc6850_receive_bit(struct mc6850_struct* p_serial, int bit);

void mc6850_receive(struct mc6850_struct* p_serial, uint8_t byte);
uint8_t mc6850_transmit(struct mc6850_struct* p_serial);

#endif /* BEEBJIT_MC6850_H */
