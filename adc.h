#ifndef BEEBJIT_ADC_H
#define BEEBJIT_ADC_H

#include <stdint.h>

uint8_t adc_read(uint8_t addr);
void adc_write(uint8_t addr, uint8_t val);

#endif /* BEEBJIT_ADC_H */
