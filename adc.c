#include "adc.h"

#include "log.h"
#include "timing.h"
#include "util.h"
#include "via.h"

#include <assert.h>

enum {
  k_adc_num_channels = 4,
};

struct adc_struct {
  int is_externally_clocked;
  struct timing_struct* p_timing;
  struct via_struct* p_system_via;
  uint32_t current_channel;
  uint16_t channel_value[k_adc_num_channels];
  uint32_t timer_id;
  uint64_t wall_time;
  uint64_t wall_time_wakeup;
  int is_input_flag;
  int is_12bit_mode;
  int is_busy;
  int is_result_ready;
};

static void
adc_stop_if_busy(struct adc_struct* p_adc) {
  if (!p_adc->is_busy) {
    return;
  }
  if (!p_adc->is_externally_clocked) {
    (void) timing_stop_timer(p_adc->p_timing, p_adc->timer_id);
  } else {
    p_adc->wall_time_wakeup = 0;
  }
  p_adc->is_busy = 0;
}

static void
adc_indicate_result_ready(struct adc_struct* p_adc) {
  assert(p_adc->is_busy);
  assert(!p_adc->is_result_ready);

  adc_stop_if_busy(p_adc);

  p_adc->is_result_ready = 1;
  via_set_CB1(p_adc->p_system_via, 0);
}

static void
adc_timer_callback(void* p) {
  struct adc_struct* p_adc = (struct adc_struct*) p;

  assert(!p_adc->is_externally_clocked);
  assert(p_adc->is_busy);

  adc_indicate_result_ready(p_adc);
}

struct adc_struct*
adc_create(int is_externally_clocked,
           struct timing_struct* p_timing,
           struct via_struct* p_system_via) {
  uint32_t i;
  struct adc_struct* p_adc = util_mallocz(sizeof(struct adc_struct));

  p_adc->is_externally_clocked = is_externally_clocked;
  p_adc->p_timing = p_timing;
  p_adc->p_system_via = p_system_via;

  p_adc->timer_id = timing_register_timer(p_timing, adc_timer_callback, p_adc);

  for (i = 0; i < k_adc_num_channels; ++i) {
    /* Default to return of 0x8000 across high and low, which is "central
     * position" for the joystick.
     */
    p_adc->channel_value[i] = 0x8000;
  }

  return p_adc;
}

static void
adc_start(struct adc_struct* p_adc, uint32_t ms) {
  assert(!p_adc->is_busy);
  if (!p_adc->is_externally_clocked) {
    (void) timing_start_timer_with_value(p_adc->p_timing,
                                         p_adc->timer_id,
                                         (ms * 2000));
  } else {
    p_adc->wall_time_wakeup = p_adc->wall_time;
    p_adc->wall_time_wakeup += (ms * 1000);
  }
  p_adc->is_busy = 1;
  p_adc->is_result_ready = 0;
  via_set_CB1(p_adc->p_system_via, 1);
}

void
adc_destroy(struct adc_struct* p_adc) {
  adc_stop_if_busy(p_adc);
  util_free(p_adc);
}

void
adc_apply_wall_time_delta(struct adc_struct* p_adc, uint64_t delta) {
  if (!p_adc->is_externally_clocked) {
    return;
  }

  p_adc->wall_time += delta;

  if (!p_adc->is_busy) {
    return;
  }

  assert(p_adc->wall_time_wakeup > 0);

  if (p_adc->wall_time >= p_adc->wall_time_wakeup) {
    adc_indicate_result_ready(p_adc);
  }
}

uint8_t
adc_read(struct adc_struct* p_adc, uint8_t addr) {
  uint8_t ret = 0;
  uint16_t adc_val = p_adc->channel_value[p_adc->current_channel];

  assert(addr <= 3);

  switch (addr) {
  case 0: /* Status. */
    ret = p_adc->current_channel;
    if (p_adc->is_input_flag) {
      ret |= 0x04;
    }
    if (p_adc->is_12bit_mode) {
      ret |= 0x08;
    }
    /* AUG states bit 4 is 2nd MSB and bit 5 MSB of conversion. */
    ret |= ((!!(adc_val & 0x8000)) * 0x20);
    ret |= ((!!(adc_val & 0x4000)) * 0x10);
    if (!p_adc->is_busy) {
      ret |= 0x40;
    }
    if (!p_adc->is_result_ready) {
      ret |= 0x80;
    }
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
  uint32_t ms;

  assert(addr <= 3);

  switch (addr) {
  case 0:
    adc_stop_if_busy(p_adc);
    p_adc->current_channel = (val & 3);
    p_adc->is_input_flag = !!(val & 0x04);
    p_adc->is_12bit_mode = !!(val & 0x08);
    /* 10ms or 4ms conversion time depending on resolution. */
    if (p_adc->is_12bit_mode) {
      ms = 10;
    } else {
      ms = 4;
    }

    adc_start(p_adc, ms);
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
