#include "adc.h"

#include "log.h"
#include "timing.h"
#include "util.h"
#include "via.h"

#include <assert.h>
#include <string.h>

enum {
  k_adc_num_channels = 4,
};

struct adc_struct {
  int is_externally_clocked;
  struct timing_struct* p_timing;
  struct via_struct* p_system_via;
  uint32_t timer_id;

  struct {
    uint8_t read_status;
    uint8_t read_hi;
    uint8_t read_lo;
    uint32_t current_channel;
    uint16_t channel_value[k_adc_num_channels];
    uint64_t wall_time;
    uint64_t wall_time_wakeup;
    int is_input_flag;
    int is_12bit_mode;
    int is_busy;
    int is_result_ready;
  } state;
};

static void
adc_stop_if_busy(struct adc_struct* p_adc) {
  if (!p_adc->state.is_busy) {
    return;
  }
  if (!p_adc->is_externally_clocked) {
    (void) timing_stop_timer(p_adc->p_timing, p_adc->timer_id);
  } else {
    p_adc->state.wall_time_wakeup = 0;
  }
  p_adc->state.is_busy = 0;
}

static void
adc_recalculate_read(struct adc_struct* p_adc) {
  uint8_t channel = p_adc->state.current_channel;
  uint16_t adc_val = p_adc->state.channel_value[channel];
  uint8_t status = channel;

  if (p_adc->state.is_input_flag) {
    status |= 0x04;
  }
  if (p_adc->state.is_12bit_mode) {
    status |= 0x08;
  }
  /* AUG states bit 4 is 2nd MSB and bit 5 MSB of conversion. */
  status |= ((!!(adc_val & 0x8000)) * 0x20);
  status |= ((!!(adc_val & 0x4000)) * 0x10);
  if (!p_adc->state.is_busy) {
    status |= 0x40;
  }
  if (!p_adc->state.is_result_ready) {
    status |= 0x80;
  }

  p_adc->state.read_status = status;
  /* TODO: we don't do anything with 8-bit vs. 10-bit conversion requests,
   * which differ in accuracy / noise.
   */
  p_adc->state.read_hi = (adc_val >> 8);
  /* AUG states bits 3-0 are always set to low. */
  p_adc->state.read_lo = (adc_val & 0xF0);
}

static void
adc_indicate_result_ready(struct adc_struct* p_adc) {
  assert(p_adc->state.is_busy);
  assert(!p_adc->state.is_result_ready);

  adc_stop_if_busy(p_adc);

  p_adc->state.is_result_ready = 1;
  via_set_CB1(p_adc->p_system_via, 0);

  adc_recalculate_read(p_adc);
}

static void
adc_timer_callback(void* p) {
  struct adc_struct* p_adc = (struct adc_struct*) p;

  assert(!p_adc->is_externally_clocked);
  assert(p_adc->state.is_busy);

  adc_indicate_result_ready(p_adc);
}

struct adc_struct*
adc_create(int is_externally_clocked,
           struct timing_struct* p_timing,
           struct via_struct* p_system_via) {
  struct adc_struct* p_adc = util_mallocz(sizeof(struct adc_struct));

  p_adc->is_externally_clocked = is_externally_clocked;
  p_adc->p_timing = p_timing;
  p_adc->p_system_via = p_system_via;

  p_adc->timer_id = timing_register_timer(p_timing,
                                          "adc",
                                          adc_timer_callback,
                                          p_adc);

  return p_adc;
}

void
adc_power_on_reset(struct adc_struct* p_adc) {
  uint32_t i;

  adc_stop_if_busy(p_adc);

  (void) memset(&p_adc->state, '\0', sizeof(p_adc->state));
  for (i = 0; i < k_adc_num_channels; ++i) {
    /* Default to return of 0x8000 across high and low, which is "central
     * position" for the joystick.
     */
    p_adc->state.channel_value[i] = 0x8000;
  }

  adc_recalculate_read(p_adc);
}

static void
adc_start(struct adc_struct* p_adc, uint32_t ms) {
  assert(!p_adc->state.is_busy);
  if (!p_adc->is_externally_clocked) {
    (void) timing_start_timer_with_value(p_adc->p_timing,
                                         p_adc->timer_id,
                                         (ms * 2000));
  } else {
    p_adc->state.wall_time_wakeup = p_adc->state.wall_time;
    p_adc->state.wall_time_wakeup += (ms * 1000);
  }
  p_adc->state.is_busy = 1;
  p_adc->state.is_result_ready = 0;
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

  p_adc->state.wall_time += delta;

  if (!p_adc->state.is_busy) {
    return;
  }

  assert(p_adc->state.wall_time_wakeup > 0);

  if (p_adc->state.wall_time >= p_adc->state.wall_time_wakeup) {
    adc_indicate_result_ready(p_adc);
  }
}

uint8_t
adc_read(struct adc_struct* p_adc, uint8_t addr) {
  assert(addr <= 3);

  switch (addr) {
  case 0:
    return p_adc->state.read_status;
  case 1:
    return p_adc->state.read_hi;
    break;
  case 2: /* ADC low. */
    return p_adc->state.read_lo;
  case 3:
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

  return 0;
}

void
adc_write(struct adc_struct* p_adc, uint8_t addr, uint8_t val) {
  uint32_t ms;

  assert(addr <= 3);

  switch (addr) {
  case 0:
    adc_stop_if_busy(p_adc);
    p_adc->state.current_channel = (val & 3);
    p_adc->state.is_input_flag = !!(val & 0x04);
    p_adc->state.is_12bit_mode = !!(val & 0x08);
    /* 10ms or 4ms conversion time depending on resolution. */
    if (p_adc->state.is_12bit_mode) {
      ms = 10;
    } else {
      ms = 4;
    }

    adc_start(p_adc, ms);

    adc_recalculate_read(p_adc);
    break;
  default:
    break;
  }
}

uint64_t
adc_write_with_countdown(struct adc_struct* p_adc,
                         uint8_t addr,
                         uint8_t val,
                         uint64_t countdown) {
  struct timing_struct* p_timing = p_adc->p_timing;
  timing_sync_countdown(p_timing, countdown);
  adc_write(p_adc, addr, val);
  return timing_get_countdown(p_timing);
}

void
adc_set_channel_value(struct adc_struct* p_adc,
                      uint32_t channel,
                      uint16_t value) {
  assert(channel < k_adc_num_channels);
  p_adc->state.channel_value[channel] = value;

  adc_recalculate_read(p_adc);
}
