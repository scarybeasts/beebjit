#ifndef BEEBJIT_INTEL_FDC_H
#define BEEBJIT_INTEL_FDC_H

#include <stdint.h>

struct intel_fdc_struct;

struct bbc_options;
struct disc_drive_struct;
struct state_6502;
struct timing_struct;

struct intel_fdc_struct* intel_fdc_create(struct state_6502* p_state_6502,
                                          struct timing_struct* p_timing,
                                          struct bbc_options* p_options);
void intel_fdc_destroy(struct intel_fdc_struct* p_fdc);

/* Setup. */
void intel_fdc_set_drives(struct intel_fdc_struct* p_fdc,
                          struct disc_drive_struct* p_drive_0,
                          struct disc_drive_struct* p_drive_1);

void intel_fdc_power_on_reset(struct intel_fdc_struct* p_fdc);
void intel_fdc_break_reset(struct intel_fdc_struct* p_fdc);

/* Host hardware register I/O. */
uint8_t intel_fdc_read(struct intel_fdc_struct* p_fdc, uint16_t addr);
void intel_fdc_write(struct intel_fdc_struct* p_fdc,
                     uint16_t addr,
                     uint8_t val);

void intel_fdc_testing_fire_nmi(struct intel_fdc_struct* p_fdc);

#endif /* BEEBJIT_INTEL_FDC_H */
