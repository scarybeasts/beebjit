#ifndef BEEBJIT_ADC_H
#define BEEBJIT_ADC_H

#include <stdint.h>

struct adc_struct;

struct adc_struct* adc_create(void);
void adc_destroy(struct adc_struct* p_adc);

uint8_t adc_read(struct adc_struct* p_adc, uint8_t addr);
void adc_write(struct adc_struct* p_adc, uint8_t addr, uint8_t val);

void adc_set_channel_value(struct adc_struct* p_adc,
                           uint32_t channel,
                           uint16_t value);

#endif /* BEEBJIT_ADC_H */
