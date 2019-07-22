#ifndef BEEBJIT_INTEL_FDC_H
#define BEEBJIT_INTEL_FDC_H

#include <stddef.h>
#include <stdint.h>

struct intel_fdc_struct;
struct state_6502;
struct timing_struct;

struct intel_fdc_struct* intel_fdc_create(struct state_6502* p_state_6502,
                                          struct timing_struct* p_timing);
void intel_fdc_destroy(struct intel_fdc_struct* p_fdc);
void intel_fdc_load_disc(struct intel_fdc_struct* p_fdc,
                         int drive,
                         int is_dsd,
                         uint8_t* p_data,
                         size_t length,
                         int writeable);

uint8_t intel_fdc_read(struct intel_fdc_struct* p_fdc, uint16_t addr);
void intel_fdc_write(struct intel_fdc_struct* p_fdc,
                     uint16_t addr,
                     uint8_t val);

void intel_fdc_timer_tick(struct intel_fdc_struct* p_fdc);

#endif /* BEEBJIT_INTEL_FDC_H */
