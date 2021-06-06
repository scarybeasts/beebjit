#ifndef BEEBJIT_SERIAL_ULA_H
#define BEEBJIT_SERIAL_ULA_H

#include <stdint.h>

struct bbc_options;
struct serial_struct;
struct serial_ula_struct;
struct tape_struct;

struct serial_ula_struct* serial_ula_create(struct serial_struct* p_serial,
                                            struct tape_struct* p_tape,
                                            int is_fasttape,
                                            struct bbc_options* p_options);
void serial_ula_destroy(struct serial_ula_struct* p_serial_ula);

void serial_ula_set_fast_mode_callback(struct serial_ula_struct* p_serial,
                                       void (*set_fast_mode_callback)(void* p,
                                                                      int fast),
                                       void* p_set_fast_mode_object);

void serial_ula_set_io_handles(struct serial_ula_struct* p_serial_ula,
                               intptr_t handle_input,
                               intptr_t handle_output);

void serial_ula_power_on_reset(struct serial_ula_struct* p_serial_ula);

uint8_t serial_ula_read(struct serial_ula_struct* p_serial_ula);
void serial_ula_write(struct serial_ula_struct* p_serial_ula, uint8_t val);

void serial_ula_receive_tape_status(struct serial_ula_struct* p_serial_ula,
                                    int is_carrier,
                                    int32_t byte);

void serial_ula_tick(struct serial_ula_struct* p_serial_ula);

#endif /* BEEBJIT_SERIAL_ULA__H */
