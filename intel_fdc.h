#ifndef BEEBJIT_INTEL_FDC_H
#define BEEBJIT_INTEL_FDC_H

#include <stdint.h>

struct intel_fdc_struct;

struct intel_fdc_struct* intel_fdc_create();
void intel_fdc_destroy(struct intel_fdc_struct* p_fdc);

uint8_t intel_fdc_read(struct intel_fdc_struct* p_fdc, uint16_t addr);
void intel_fdc_write(struct intel_fdc_struct* p_fdc,
                     uint16_t addr,
                     uint8_t val);

#endif /* BEEBJIT_INTEL_FDC_H */
