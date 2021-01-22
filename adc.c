#include "adc.h"

#include "log.h"

#include <assert.h>

uint8_t
adc_read(uint8_t addr) {
  uint8_t ret = 0;

  switch (addr) {
  case 0: /* Status. */
    /* Return ADC conversion complete (bit 6). */
    ret = 0x40;
    break;
  case 1: /* ADC high. */
    /* Return 0x8000 across high and low, which is "central position" for the
     * joystick.
     */
    ret = 0x80;
    break;
  case 2: /* ADC low. */
    ret = 0;
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
adc_write(uint8_t addr, uint8_t val) {
  (void) addr;
  (void) val;
}
