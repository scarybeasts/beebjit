#ifndef BEEBJIT_ADC_H
#define BEEBJIT_ADC_H

#include <stdint.h>

struct timing_struct;
struct via_struct;

struct adc_struct;

struct adc_struct* adc_create(int is_externally_clocked,
                              struct timing_struct* p_timing,
                              struct via_struct* p_system_via);
void adc_destroy(struct adc_struct* p_adc);

void adc_power_on_reset(struct adc_struct* p_adc);
void adc_apply_wall_time_delta(struct adc_struct* p_adc, uint64_t delta);

uint8_t adc_read(struct adc_struct* p_adc, uint8_t addr);
void adc_write(struct adc_struct* p_adc, uint8_t addr, uint8_t val);

void adc_set_channel_value(struct adc_struct* p_adc,
                           uint32_t channel,
                           uint16_t value);

#endif /* BEEBJIT_ADC_H */
