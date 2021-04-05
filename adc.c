#include "adc.h"

#include "log.h"
#include "util.h"
#include "via.h"

#include <assert.h>

enum {
  k_adc_num_channels = 4,
};

struct adc_struct {
  struct via_struct* p_system_via;
  uint32_t current_channel;
  uint16_t channel_value[k_adc_num_channels];
};

struct adc_struct*
adc_create(struct via_struct* p_system_via) {
  uint32_t i;
  struct adc_struct* p_adc = util_mallocz(sizeof(struct adc_struct));

  p_adc->p_system_via = p_system_via;

  for (i = 0; i < k_adc_num_channels; ++i) {
    /* Default to return of 0x8000 across high and low, which is "central
     * position" for the joystick.
     */
    p_adc->channel_value[i] = 0x8000;
  }

  return p_adc;
}

void
adc_destroy(struct adc_struct* p_adc) {
  util_free(p_adc);
}

uint8_t
adc_read(struct adc_struct* p_adc, uint8_t addr) {
  uint8_t ret = 0;
  uint16_t adc_val = p_adc->channel_value[p_adc->current_channel];

  assert(addr <= 3);

  switch (addr) {
  case 0: /* Status. */
    /* Return ADC not busy and conversion completed (bits 6 / 7).
     * TODO: never returns busy. Conversions are instant, which is an
     * inaccuracy of course.
     */
    ret = 0x40;
    ret |= p_adc->current_channel;
    /* AUG states bit 4 is 2nd MSB and bit 5 MSB of conversion. */
    ret |= ((!!(adc_val & 0x8000)) * 0x20);
    ret |= ((!!(adc_val & 0x4000)) * 0x10);
    break;
  case 1: /* ADC high. */
    ret = (adc_val >> 8);
    break;
  case 2: /* ADC low. */
    ret = (adc_val & 0xFF);
    /* AUG states bits 3-0 are always set to low. */
    ret &= 0xF0;
    /* TODO: we don't do anything with 8-bit vs. 10-bit conversion requests,
     * which differ in accuracy / noise.
     */
    break;
  case 3:
    ret = 0;
    {
      static uint32_t s_max_log_count = 4;
      log_do_log_max_count(&s_max_log_count,
                           k_log_misc,
                           k_log_unimplemented,
                           "ADC read of index 3");
    }
    break;
  default:
    assert(0);
    break;
  }

  return ret;
}

void
adc_write(struct adc_struct* p_adc, uint8_t addr, uint8_t val) {
  struct via_struct* p_system_via;

  assert(addr <= 3);

  switch (addr) {
  case 0:
    p_adc->current_channel = (val & 3);
    /* NOTE: our converstions are instant, which is inaccurate, so the interrupt
     * line gets raised immediately.
     */
    p_system_via = p_adc->p_system_via;
    via_set_CB1(p_system_via, 1);
    via_set_CB1(p_system_via, 0);
    break;
  default:
    break;
  }
}

void
adc_set_channel_value(struct adc_struct* p_adc,
                      uint32_t channel,
                      uint16_t value) {
  assert(channel < k_adc_num_channels);
  p_adc->channel_value[channel] = value;
}
