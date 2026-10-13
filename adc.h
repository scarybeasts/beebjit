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
uint64_t adc_write_control_with_countdown(struct adc_struct* p_adc,
                                          uint8_t addr,
                                          uint8_t val,
                                          uint64_t countdown);

void adc_set_channel_value(struct adc_struct* p_adc,
                           uint32_t channel,
                           uint16_t value);

/* To carry on exactly after a state save and load. It is stored in state
 * files as is, so a change to it needs a new state version.
 */
struct adc_state {
  int64_t conversion_ticks_left;
  uint16_t channel_value[4];
  uint8_t current_channel;
  uint8_t is_input_flag;
  uint8_t is_12bit_mode;
  uint8_t is_busy;
  uint8_t is_result_ready;
};

void adc_get_state(struct adc_struct* p_adc, struct adc_state* p_state);
void adc_set_state(struct adc_struct* p_adc, const struct adc_state* p_state);

#endif /* BEEBJIT_ADC_H */
