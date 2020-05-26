#include "wd_fdc.h"

#include "bbc_options.h"
#include "disc_drive.h"
#include "log.h"
#include "state_6502.h"
#include "util.h"

#include <assert.h>

static const uint32_t k_wd_fdc_1770_settle_ticks = ((30 * 1000) / 64);

enum {
  k_wd_fdc_command_restore = 0x00,
  k_wd_fdc_command_read_sector = 0x80,
  k_wd_fdc_command_force_interrupt = 0xD0,
};

enum {
  k_wd_fdc_command_bit_disable_spin_up = 0x08,
  k_wd_fdc_command_bit_type_I_verify = 0x04,
  k_wd_fdc_command_bit_type_II_III_settle = 0x04,
};

enum {
  k_wd_fdc_control_reset = 0x20,
  k_wd_fdc_control_density = 0x08,
  k_wd_fdc_control_drive_1 = 0x02,
  k_wd_fdc_control_drive_0 = 0x01,
};

enum {
  k_wd_fdc_status_motor_on = 0x80,
  k_wd_fdc_status_type_I_spin_up_done = 0x20,
  k_wd_fdc_status_type_I_track_0 = 0x04,
  k_wd_fdc_status_type_I_index = 0x02,
  k_wd_fdc_status_busy = 0x01,
};

enum {
  k_wd_fdc_state_null = 0,
  k_wd_fdc_state_idle,
  k_wd_fdc_state_spin_up_wait,
  k_wd_fdc_state_settle,
  k_wd_fdc_state_settle_wait,
  k_wd_fdc_state_seek_step,
  k_wd_fdc_state_seek_step_wait,
};

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

  struct disc_drive_struct* p_current_drive;
  int is_index_pulse;
  uint8_t command;
  uint8_t command_type;
  int is_command_settle;
  uint32_t state;
  uint32_t state_count;
  uint32_t index_pulse_count;
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

static void
wd_fdc_set_intrq(struct wd_fdc_struct* p_fdc, int level) {
  struct state_6502* p_state_6502 = p_fdc->p_state_6502;
  int firing = state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi);

  if (firing && (level == 1)) {
    log_do_log(k_log_disc, k_log_error, "edge triggered NMI already high");
  }

  state_6502_set_irq_level(p_state_6502, k_state_6502_irq_nmi, level);
}

static void
wd_fdc_command_done(struct wd_fdc_struct* p_fdc) {
  assert(p_fdc->status_register & k_wd_fdc_status_busy);
  assert(p_fdc->state > k_wd_fdc_state_idle);

  p_fdc->status_register &= ~k_wd_fdc_status_busy;
  p_fdc->state = k_wd_fdc_state_idle;
  p_fdc->index_pulse_count = 0;

  wd_fdc_set_intrq(p_fdc, 1);

  if (p_fdc->log_commands) {
    log_do_log(k_log_disc,
               k_log_info,
               "1770: result status $%.2X",
               p_fdc->status_register);
  }
}

static void
wd_fdc_do_command(struct wd_fdc_struct* p_fdc, uint8_t val) {
  uint8_t command;

  if (p_fdc->log_commands) {
    log_do_log(k_log_disc,
               k_log_info,
               "1770: command $%.2X tr %d sr %d dr %d cr $%.2X",
               val,
               p_fdc->track_register,
               p_fdc->sector_register,
               p_fdc->data_register,
               p_fdc->control_register);
  }

  if (p_fdc->p_current_drive == NULL) {
    util_bail("command while no selected drive");
  }
  if (p_fdc->status_register & k_wd_fdc_status_busy) {
    util_bail("command while busy");
  }
  if (!(p_fdc->control_register & k_wd_fdc_control_reset)) {
    util_bail("command while in reset");
  }
  if (!(p_fdc->control_register & k_wd_fdc_control_density)) {
    util_bail("command while double density");
  }

  command = (val & 0x80);
  p_fdc->command = command;
  p_fdc->is_command_settle = 0;

  switch (command) {
  case k_wd_fdc_command_restore:
    p_fdc->command_type = 1;
    break;
  case k_wd_fdc_command_read_sector:
    p_fdc->command_type = 2;
    break;
  default:
    util_bail("unimplemented command $%X", val);
    break;
  }
  if (((p_fdc->command_type == 2) || (p_fdc->command_type == 3)) &&
      (val & k_wd_fdc_command_bit_type_II_III_settle)) {
    p_fdc->is_command_settle = 1;
  }

  if (command == k_wd_fdc_command_force_interrupt) {
    util_bail("force interrupt");
  }

  /* All commands except force interrupt (handled above):
   * - Clear INTRQ.
   * - Clear status register result bits.
   * - Set busy.
   * - Spin up if necessary and not inhibited.
   */
  wd_fdc_set_intrq(p_fdc, 0);
  p_fdc->status_register &= k_wd_fdc_status_motor_on;
  p_fdc->status_register |= k_wd_fdc_status_busy;
  if (val & k_wd_fdc_command_bit_disable_spin_up) {
    util_bail("spin up disabled");
  }

  p_fdc->index_pulse_count = 0;
  if (p_fdc->status_register & k_wd_fdc_status_motor_on) {
    /* Short circuit spin-up if motor is on. */
    p_fdc->index_pulse_count = 6;
  } else {
    p_fdc->status_register |= k_wd_fdc_status_motor_on;
    disc_drive_start_spinning(p_fdc->p_current_drive);
  }

  p_fdc->state = k_wd_fdc_state_spin_up_wait;
}

uint8_t
wd_fdc_read(struct wd_fdc_struct* p_fdc, uint16_t addr) {
  uint8_t ret;

  switch (addr) {
  case 4:
    /* Reading status register clears INTRQ. */
    wd_fdc_set_intrq(p_fdc, 0);
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
  struct disc_drive_struct* p_current_drive;
  int is_motor_on;

  switch (addr) {
  case 0:
  case 1:
  case 2:
  case 3:
    p_current_drive = p_fdc->p_current_drive;
    is_motor_on = !!(p_fdc->status_register & k_wd_fdc_status_motor_on);

    if (p_fdc->log_commands) {
      log_do_log(k_log_disc,
                 k_log_info,
                 "1770: control register now $%.2X",
                 val);
    }
    if (p_fdc->status_register & k_wd_fdc_status_busy) {
      util_bail("control register updated while busy");
    }
    if (p_current_drive != NULL) {
      if (disc_drive_is_spinning(p_current_drive)) {
        assert(is_motor_on);
        disc_drive_stop_spinning(p_current_drive);
      }
    }
    if ((val & k_wd_fdc_control_drive_0) ^ (val & k_wd_fdc_control_drive_1)) {
      if (val & k_wd_fdc_control_drive_0) {
        p_fdc->p_current_drive = p_fdc->p_drive_0;
      } else {
        p_fdc->p_current_drive = p_fdc->p_drive_1;
      }
    } else {
      p_fdc->p_current_drive = NULL;
    }
    p_fdc->control_register = val;
    break;
  case 4:
    wd_fdc_do_command(p_fdc, val);
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
wd_fdc_update_type_I_status_bits(struct wd_fdc_struct* p_fdc) {
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  if (p_fdc->command_type != 1) {
    return;
  }

  p_fdc->status_register &=
      ~(k_wd_fdc_status_type_I_track_0 | k_wd_fdc_status_type_I_index);
  if (disc_drive_get_track(p_current_drive) == 0) {
    p_fdc->status_register |= k_wd_fdc_status_type_I_track_0;
  }
  if (disc_drive_is_index_pulse(p_current_drive)) {
    p_fdc->status_register |= k_wd_fdc_status_type_I_index;
  }
}

static void
wd_fdc_byte_callback(void* p, uint8_t data_byte, uint8_t clocks_byte) {
  struct wd_fdc_struct* p_fdc = (struct wd_fdc_struct*) p;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  int was_index_pulse = p_fdc->is_index_pulse;

  (void) data_byte;
  (void) clocks_byte;

  assert(p_current_drive != NULL);
  assert(disc_drive_is_spinning(p_current_drive));
  assert(p_fdc->status_register & k_wd_fdc_status_motor_on);

  p_fdc->is_index_pulse = disc_drive_is_index_pulse(p_fdc->p_current_drive);
  if (p_fdc->is_index_pulse && !was_index_pulse) {
    p_fdc->index_pulse_count++;
  }

  /* EMU NOTE: if the chip is idle after completion of a type I command), the
   * index pulse and track 0 bits appear maintained. They disappear on
   * spin-down.
   */
  wd_fdc_update_type_I_status_bits(p_fdc);

  switch (p_fdc->state) {
  case k_wd_fdc_state_idle:
    assert(!(p_fdc->status_register & k_wd_fdc_status_busy));
    /* EMU NOTE: different sources disagree on 10 vs. 9 index pulses for
     * spin down.
     */
    if (p_fdc->index_pulse_count == 10) {
      if (p_fdc->log_commands) {
        log_do_log(k_log_disc, k_log_info, "1770: automatic motor off");
      }
      disc_drive_stop_spinning(p_current_drive);
      p_fdc->status_register &= ~k_wd_fdc_status_motor_on;
      /* EMU TODO: other bits get cleared? */
    }
    break;
  case k_wd_fdc_state_spin_up_wait:
    if (p_fdc->index_pulse_count != 6) {
      break;
    }
    if (p_fdc->command_type == 1) {
      p_fdc->status_register |= k_wd_fdc_status_type_I_spin_up_done;
    }
    p_fdc->state = k_wd_fdc_state_settle;
    break;
  case k_wd_fdc_state_settle:
    p_fdc->state_count = 0;
    if (!p_fdc->is_command_settle) {
      /* Short circuit settle if not selected. */
      p_fdc->state_count = k_wd_fdc_1770_settle_ticks;
    }
    p_fdc->state = k_wd_fdc_state_settle_wait;
    break;
  case k_wd_fdc_state_settle_wait:
    if (p_fdc->state_count != k_wd_fdc_1770_settle_ticks) {
      p_fdc->state_count++;
      break;
    }
    switch (p_fdc->command) {
    case k_wd_fdc_command_restore:
      p_fdc->track_register = 0xFF;
      p_fdc->data_register = 0;
      p_fdc->state = k_wd_fdc_state_seek_step;
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_wd_fdc_state_seek_step:
    if (disc_drive_get_track(p_current_drive) == 0) {
      p_fdc->track_register = 0;
      wd_fdc_command_done(p_fdc);
    } else {
      disc_drive_seek_track(p_current_drive, -1);
      /* TRK0 signal may have raised or lowered. */
      wd_fdc_update_type_I_status_bits(p_fdc);
      util_bail("waaaaa");
    }
    break;
  default:
    assert(0);
    break;
  }
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
