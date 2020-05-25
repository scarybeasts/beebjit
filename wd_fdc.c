#include "wd_fdc.h"

#include "bbc_options.h"
#include "disc_drive.h"
#include "util.h"

#include <assert.h>

struct wd_fdc_struct {
  struct state_6502* p_state_6502;

  int log_commands;

  struct disc_drive_struct* p_drive_0;
  struct disc_drive_struct* p_drive_1;

  uint8_t control_register;
  uint8_t status_register;
  uint8_t track_register;
  uint8_t sector_register;
  uint8_t data_register;
};

struct wd_fdc_struct*
wd_fdc_create(struct state_6502* p_state_6502, struct bbc_options* p_options) {
  struct wd_fdc_struct* p_fdc = util_mallocz(sizeof(struct wd_fdc_struct));

  p_fdc->p_state_6502 = p_state_6502;

  p_fdc->log_commands = util_has_option(p_options->p_log_flags,
                                        "disc:commands");

  return p_fdc;
}

void
wd_fdc_destroy(struct wd_fdc_struct* p_fdc) {
  struct disc_drive_struct* p_drive_0 = p_fdc->p_drive_0;
  struct disc_drive_struct* p_drive_1 = p_fdc->p_drive_1;

  disc_drive_set_byte_callback(p_drive_0, NULL, NULL);
  disc_drive_set_byte_callback(p_drive_1, NULL, NULL);

  if (disc_drive_is_spinning(p_drive_0)) {
    disc_drive_stop_spinning(p_drive_0);
  }
  if (disc_drive_is_spinning(p_drive_1)) {
    disc_drive_stop_spinning(p_drive_1);
  }

  util_free(p_fdc);
}

void
wd_fdc_break_reset(struct wd_fdc_struct* p_fdc) {
  /* TODO: abort command etc. */
  (void) p_fdc;
}

void
wd_fdc_power_on_reset(struct wd_fdc_struct* p_fdc) {
  wd_fdc_break_reset(p_fdc);
  p_fdc->control_register = 0;
  /* EMU NOTE: my WD1772 appears to have some non-zero values in some of these
   * registers at power on. It's not known if that's just randomness or
   * something else.
   */
  p_fdc->status_register = 0;
  p_fdc->track_register = 0;
  p_fdc->sector_register = 0;
  p_fdc->data_register = 0;

}

uint8_t
wd_fdc_read(struct wd_fdc_struct* p_fdc, uint16_t addr) {
  uint8_t ret;

  switch (addr) {
  case 4:
    ret = p_fdc->status_register;
    break;
  case 5:
    ret = p_fdc->track_register;
    break;
  case 6:
    ret = p_fdc->sector_register;
    break;
  case 7:
    ret = p_fdc->data_register;
    break;
  default:
    assert(0);
    break;
  }

  return ret;
}

void
wd_fdc_write(struct wd_fdc_struct* p_fdc, uint16_t addr, uint8_t val) {
  switch (addr) {
  case 0:
  case 1:
  case 2:
  case 3:
    p_fdc->control_register = val;
    break;
  case 4:
    break;
  case 5:
    p_fdc->track_register = val;
    break;
  case 6:
    p_fdc->sector_register = val;
    break;
  case 7:
    p_fdc->data_register = val;
    break;
  default:
    assert(0);
    break;
  }
}

static void
wd_fdc_byte_callback(void* p, uint8_t data_byte, uint8_t clocks_byte) {
  (void) p;
  (void) data_byte;
  (void) clocks_byte;
}

void
wd_fdc_set_drives(struct wd_fdc_struct* p_fdc,
                  struct disc_drive_struct* p_drive_0,
                  struct disc_drive_struct* p_drive_1) {
  assert(p_fdc->p_drive_0 == NULL);
  assert(p_fdc->p_drive_1 == NULL);
  p_fdc->p_drive_0 = p_drive_0;
  p_fdc->p_drive_1 = p_drive_1;

  disc_drive_set_byte_callback(p_drive_0, wd_fdc_byte_callback, p_fdc);
  disc_drive_set_byte_callback(p_drive_1, wd_fdc_byte_callback, p_fdc);
}
