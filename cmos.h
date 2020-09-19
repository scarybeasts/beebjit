#ifndef BEEBJIT_CMOS_H
#define BEEBJIT_CMOS_H

#include <stdint.h>

struct cmos_struct;

struct bbc_options;

struct cmos_struct* cmos_create(struct bbc_options* p_options);
void cmos_destroy(struct cmos_struct* p_cmos);

uint8_t cmos_get_bus_value(struct cmos_struct* p_cmos);
void cmos_update_external_inputs(struct cmos_struct* p_cmos,
                                 uint8_t port_b,
                                 uint8_t port_a,
                                 uint8_t IC32);

#endif /* BEEBJIT_CMOS_H */
