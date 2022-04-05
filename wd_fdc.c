#include "wd_fdc.h"

#include "bbc_options.h"
#include "disc_drive.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"

#include <assert.h>

static const uint32_t k_wd_fdc_1770_settle_ms = 30;

enum {
  k_wd_fdc_command_restore = 0x00,
  k_wd_fdc_command_seek = 0x10,
  k_wd_fdc_command_step_in_with_update = 0x50,
  k_wd_fdc_command_step_out_with_update = 0x70,
  k_wd_fdc_command_read_sector = 0x80,
  k_wd_fdc_command_read_sector_multi = 0x90,
  k_wd_fdc_command_write_sector = 0xA0,
  k_wd_fdc_command_write_sector_multi = 0xB0,
  k_wd_fdc_command_read_address = 0xC0,
  k_wd_fdc_command_force_interrupt = 0xD0,
  k_wd_fdc_command_read_track = 0xE0,
  k_wd_fdc_command_write_track = 0xF0,
};

enum {
  k_wd_fdc_command_bit_type_II_multi = 0x10,
  k_wd_fdc_command_bit_disable_spin_up = 0x08,
  k_wd_fdc_command_bit_type_I_verify = 0x04,
  k_wd_fdc_command_bit_type_II_III_settle = 0x04,
  k_wd_fdc_command_bit_type_II_deleted = 0x01,
};

/* The drive control register is documented here:
 * https://www.cloud9.co.uk/james/BBCMicro/Documentation/wd1770.html
 */
enum {
  k_wd_fdc_control_reset = 0x20,
  k_wd_fdc_control_density = 0x08,
  k_wd_fdc_control_side = 0x04,
  k_wd_fdc_control_drive_1 = 0x02,
  k_wd_fdc_control_drive_0 = 0x01,
};

enum {
  k_wd_fdc_status_motor_on = 0x80,
  k_wd_fdc_status_write_protected = 0x40,
  k_wd_fdc_status_type_I_spin_up_done = 0x20,
  k_wd_fdc_status_type_II_III_deleted_mark = 0x20,
  k_wd_fdc_status_record_not_found = 0x10,
  k_wd_fdc_status_crc_error = 0x08,
  k_wd_fdc_status_type_I_track_0 = 0x04,
  k_wd_fdc_status_type_II_III_lost_byte = 0x04,
  k_wd_fdc_status_type_I_index = 0x02,
  k_wd_fdc_status_type_II_III_drq = 0x02,
  k_wd_fdc_status_busy = 0x01,
};

enum {
  k_wd_fdc_state_null = 0,
  k_wd_fdc_state_idle,
  k_wd_fdc_state_timer_wait,
  k_wd_fdc_state_spin_up_wait,
  k_wd_fdc_state_wait_index,
  k_wd_fdc_state_search_id,
  k_wd_fdc_state_in_id,
  k_wd_fdc_state_search_data,
  k_wd_fdc_state_in_data,
  k_wd_fdc_state_in_read_track,
  k_wd_fdc_state_write_sector_delay,
  k_wd_fdc_state_write_sector_lead_in_fm,
  k_wd_fdc_state_write_sector_lead_in_mfm,
  k_wd_fdc_state_write_sector_marker_fm,
  k_wd_fdc_state_write_sector_marker_mfm,
  k_wd_fdc_state_write_sector_body,
  k_wd_fdc_state_write_track_setup,
  k_wd_fdc_state_in_write_track,
  k_wd_fdc_state_check_multi,
  k_wd_fdc_state_done,
};

enum {
  k_wd_fdc_timer_none = 1,
  k_wd_fdc_timer_settle = 2,
  k_wd_fdc_timer_seek = 3,
  k_wd_fdc_timer_done = 4,
};

struct wd_fdc_struct {
  struct state_6502* p_state_6502;
  int is_master;
  int is_1772;
  int is_opus;
  struct timing_struct* p_timing;
  uint32_t timer_id;

  int log_commands;

  struct disc_drive_struct* p_drive_0;
  struct disc_drive_struct* p_drive_1;

  uint8_t control_register;
  uint8_t status_register;
  uint8_t track_register;
  uint8_t sector_register;
  uint8_t data_register;
  int is_intrq;
  int is_drq;
  int do_raise_intrq;

  struct disc_drive_struct* p_current_drive;
  int is_index_pulse;
  int is_interrupt_on_index_pulse;
  int is_write_track_crc_second_byte;
  uint8_t command;
  uint8_t command_type;
  int is_command_settle;
  int is_command_write;
  int is_command_verify;
  int is_command_multi;
  int is_command_deleted;
  uint32_t command_step_rate_ms;
  uint32_t state;
  uint32_t timer_state;
  uint32_t state_count;
  uint32_t index_pulse_count;
  uint64_t mark_detector;
  uint32_t data_shifter;
  uint32_t data_shift_count;
  uint8_t deliver_data;
  int deliver_is_marker;
  uint16_t crc;
  uint8_t on_disc_track;
  uint8_t on_disc_sector;
  uint32_t on_disc_length;
  uint16_t on_disc_crc;
  int last_mfm_bit;
};

static void
wd_fdc_clear_timer(struct wd_fdc_struct* p_fdc) {
  if (timing_timer_is_running(p_fdc->p_timing, p_fdc->timer_id)) {
    assert(p_fdc->timer_state != k_wd_fdc_timer_none);
    (void) timing_stop_timer(p_fdc->p_timing, p_fdc->timer_id);
    p_fdc->timer_state = k_wd_fdc_timer_none;
  }
}

static void
wd_fdc_set_state(struct wd_fdc_struct* p_fdc, int state) {
  p_fdc->state = state;
  p_fdc->state_count = 0;
}

static void
wd_fdc_clear_state(struct wd_fdc_struct* p_fdc) {
  wd_fdc_set_state(p_fdc, k_wd_fdc_state_idle);
  wd_fdc_clear_timer(p_fdc);
  p_fdc->index_pulse_count = 0;
}

static void
wd_fdc_update_nmi(struct wd_fdc_struct* p_fdc) {
  struct state_6502* p_state_6502 = p_fdc->p_state_6502;
  int firing = state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi);
  int old_level = state_6502_get_irq_level(p_state_6502, k_state_6502_irq_nmi);
  int new_level = p_fdc->is_drq;
  if (!p_fdc->is_opus) {
    new_level |= p_fdc->is_intrq;
  }

  if (new_level && !old_level && firing) {
    log_do_log(k_log_disc, k_log_error, "1770 lost NMI positive edge");
  }

  state_6502_set_irq_level(p_state_6502, k_state_6502_irq_nmi, new_level);
}

static void
wd_fdc_set_intrq(struct wd_fdc_struct* p_fdc, int level) {
  p_fdc->is_intrq = level;
  wd_fdc_update_nmi(p_fdc);
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
wd_fdc_start_timer(struct wd_fdc_struct* p_fdc,
                   int timer_state,
                   uint32_t wait_us) {
  assert(p_fdc->status_register & k_wd_fdc_status_busy);
  assert(p_fdc->timer_state == k_wd_fdc_timer_none);
  p_fdc->timer_state = timer_state;
  p_fdc->state = k_wd_fdc_state_timer_wait;
  (void) timing_start_timer_with_value(p_fdc->p_timing,
                                       p_fdc->timer_id,
                                       (wait_us * 2));
}

static void
wd_fdc_command_done(struct wd_fdc_struct* p_fdc, int do_raise_intrq) {
  assert(p_fdc->status_register & k_wd_fdc_status_busy);

  p_fdc->do_raise_intrq = do_raise_intrq;

  wd_fdc_start_timer(p_fdc, k_wd_fdc_timer_done, 32);
}

static void
wd_fdc_command_done_timer(struct wd_fdc_struct* p_fdc) {
  p_fdc->status_register &= ~k_wd_fdc_status_busy;
  wd_fdc_clear_state(p_fdc);

  /* Make sure the status are up to date. */
  wd_fdc_update_type_I_status_bits(p_fdc);

  /* EMU NOTE: leave DRQ alone, if it is raised leave it raised. */
  if (p_fdc->do_raise_intrq) {
    wd_fdc_set_intrq(p_fdc, 1);
  }

  if (p_fdc->log_commands) {
    log_do_log(k_log_disc,
               k_log_info,
               "1770: result status $%.2X",
               p_fdc->status_register);
  }
}

static void
wd_fdc_check_verify(struct wd_fdc_struct* p_fdc) {
  if (p_fdc->is_command_verify) {
    p_fdc->index_pulse_count = 0;
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_id);
  } else {
    wd_fdc_command_done(p_fdc, 1);
  }
}

static void
wd_fdc_do_seek_step(struct wd_fdc_struct* p_fdc, int step_direction) {
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  assert(p_current_drive != NULL);

  disc_drive_seek_track(p_current_drive, step_direction);
  p_fdc->track_register += step_direction;
  /* TRK0 signal may have raised or lowered. */
  wd_fdc_update_type_I_status_bits(p_fdc);
  wd_fdc_start_timer(p_fdc,
                     k_wd_fdc_timer_seek,
                     (p_fdc->command_step_rate_ms * 1000));
}

static void
wd_fdc_do_seek_step_or_verify(struct wd_fdc_struct* p_fdc) {
  int step_direction;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;

  assert(p_current_drive != NULL);

  if (p_fdc->track_register == p_fdc->data_register) {
    wd_fdc_check_verify(p_fdc);
    return;
  }

  if (p_fdc->track_register > p_fdc->data_register) {
    step_direction = -1;
  } else {
    step_direction = 1;
  }
  if ((disc_drive_get_track(p_current_drive) == 0) &&
      (step_direction == -1)) {
    p_fdc->track_register = 0;
    wd_fdc_check_verify(p_fdc);
    return;
  }

  wd_fdc_do_seek_step(p_fdc, step_direction);
}

static void
wd_fdc_dispatch_command(struct wd_fdc_struct* p_fdc) {
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;

  assert(p_current_drive != NULL);

  if (p_fdc->is_command_write && disc_drive_is_write_protect(p_current_drive)) {
    p_fdc->status_register |= k_wd_fdc_status_write_protected;
    wd_fdc_command_done(p_fdc, 1);
    return;
  }

  switch (p_fdc->command) {
  case k_wd_fdc_command_restore:
    p_fdc->track_register = 0xFF;
    p_fdc->data_register = 0;
    /* Fall through. */
  case k_wd_fdc_command_seek:
    wd_fdc_do_seek_step_or_verify(p_fdc);
    break;
  case k_wd_fdc_command_step_in_with_update:
    wd_fdc_do_seek_step(p_fdc, 1);
    break;
  case k_wd_fdc_command_step_out_with_update:
    wd_fdc_do_seek_step(p_fdc, -1);
    break;
  case k_wd_fdc_command_read_sector:
  case k_wd_fdc_command_read_sector_multi:
  case k_wd_fdc_command_write_sector:
  case k_wd_fdc_command_write_sector_multi:
  case k_wd_fdc_command_read_address:
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_id);
    p_fdc->index_pulse_count = 0;
    break;
  case k_wd_fdc_command_read_track:
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_wait_index);
    p_fdc->index_pulse_count = 0;
    break;
  case k_wd_fdc_command_write_track:
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_write_track_setup);
    p_fdc->index_pulse_count = 0;
    break;
  default:
    assert(0);
    break;
  }
}

static void
wd_fdc_timer_fired(void* p) {
  struct wd_fdc_struct* p_fdc = (struct wd_fdc_struct*) p;
  uint32_t timer_state = p_fdc->timer_state;

  assert(p_fdc->status_register & k_wd_fdc_status_busy);

  (void) timing_stop_timer(p_fdc->p_timing, p_fdc->timer_id);
  p_fdc->timer_state = k_wd_fdc_timer_none;

  switch (timer_state) {
  case k_wd_fdc_timer_settle:
    wd_fdc_dispatch_command(p_fdc);
    assert(p_fdc->state != k_wd_fdc_state_timer_wait);
    break;
  case k_wd_fdc_timer_seek:
    if ((p_fdc->command == k_wd_fdc_command_step_in_with_update) ||
        (p_fdc->command == k_wd_fdc_command_step_out_with_update)) {
      wd_fdc_check_verify(p_fdc);
    } else {
      wd_fdc_do_seek_step_or_verify(p_fdc);
    }
    break;
  case k_wd_fdc_timer_done:
    wd_fdc_command_done_timer(p_fdc);
    break;
  default:
    assert(0);
    break;
  }
}

struct wd_fdc_struct*
wd_fdc_create(struct state_6502* p_state_6502,
              int is_master,
              int is_1772,
              struct timing_struct* p_timing,
              struct bbc_options* p_options) {
  struct wd_fdc_struct* p_fdc = util_mallocz(sizeof(struct wd_fdc_struct));

  p_fdc->p_state_6502 = p_state_6502;
  p_fdc->is_master = is_master;
  p_fdc->is_1772 = is_1772;
  p_fdc->p_timing = p_timing;
  p_fdc->timer_id = timing_register_timer(p_timing,
                                          wd_fdc_timer_fired,
                                          p_fdc);

  p_fdc->state = k_wd_fdc_state_idle;
  p_fdc->timer_state = k_wd_fdc_timer_none;

  p_fdc->log_commands = util_has_option(p_options->p_log_flags,
                                        "disc:commands");

  return p_fdc;
}

void
wd_fdc_destroy(struct wd_fdc_struct* p_fdc) {
  struct disc_drive_struct* p_drive_0 = p_fdc->p_drive_0;
  struct disc_drive_struct* p_drive_1 = p_fdc->p_drive_1;

  disc_drive_set_pulses_callback(p_drive_0, NULL, NULL);
  disc_drive_set_pulses_callback(p_drive_1, NULL, NULL);

  if (disc_drive_is_spinning(p_drive_0)) {
    disc_drive_stop_spinning(p_drive_0);
  }
  if (disc_drive_is_spinning(p_drive_1)) {
    disc_drive_stop_spinning(p_drive_1);
  }
  if (timing_timer_is_running(p_fdc->p_timing, p_fdc->timer_id)) {
    (void) timing_stop_timer(p_fdc->p_timing, p_fdc->timer_id);
  }

  util_free(p_fdc);
}

static int
wd_fdc_is_reset(uint8_t cr) {
  /* Reset is active low. */
  return !(cr & k_wd_fdc_control_reset);
}

static int
wd_fdc_is_side(uint8_t cr) {
  return !!(cr & k_wd_fdc_control_side);
}

static int
wd_fdc_is_double_density(uint8_t cr) {
  /* Double density (aka. MFM) is active low. */
  return !(cr & k_wd_fdc_control_density);
}

static void
wd_fdc_write_control(struct wd_fdc_struct* p_fdc, uint8_t val) {
  int is_double_density;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  int is_motor_on = !!(p_fdc->status_register & k_wd_fdc_status_motor_on);

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
  if (p_fdc->p_current_drive != NULL) {
    if (is_motor_on) {
      disc_drive_start_spinning(p_fdc->p_current_drive);
    }
    disc_drive_select_side(p_fdc->p_current_drive, wd_fdc_is_side(val));
  }

  /* Set up single or double density. */
  is_double_density = wd_fdc_is_double_density(val);
  disc_drive_set_32us_mode(p_fdc->p_drive_0, is_double_density);
  disc_drive_set_32us_mode(p_fdc->p_drive_1, is_double_density);

  p_fdc->control_register = val;

  /* Reset, active low. */
  if (wd_fdc_is_reset(val)) {
    /* Go idle, etc. */
    wd_fdc_clear_state(p_fdc);
    if (p_fdc->p_current_drive != NULL) {
      if (is_motor_on) {
        disc_drive_stop_spinning(p_fdc->p_current_drive);
      }
    }
    p_fdc->status_register = 0;
    /* EMU NOTE: on a real machine, the reset condition appears to hold the
     * sector register at 1 but leave track / data alone (and permit changes
     * to them).
     */
    p_fdc->sector_register = 1;
    p_fdc->is_intrq = 0;
    p_fdc->is_drq = 0;
    wd_fdc_update_nmi(p_fdc);

    p_fdc->mark_detector = 0;
    p_fdc->data_shifter = 0;
    p_fdc->data_shift_count = 0;
    p_fdc->is_index_pulse = 0;
    p_fdc->last_mfm_bit = 0;
    p_fdc->deliver_data = 0;
    p_fdc->deliver_is_marker = 0;
  }
}

void
wd_fdc_break_reset(struct wd_fdc_struct* p_fdc) {
  /* This will:
   * - Spin down.
   * - Raise reset, which:
   *   - Clears status register.
   *   - Sets other registers as per how a real machine behaves.
   *   - Clears IRQs.
   */
  wd_fdc_write_control(p_fdc, 0);
}

void
wd_fdc_power_on_reset(struct wd_fdc_struct* p_fdc) {
  wd_fdc_break_reset(p_fdc);
  assert(p_fdc->control_register == 0);
  assert(p_fdc->status_register == 0);
  assert(p_fdc->state == k_wd_fdc_state_idle);
  assert(p_fdc->timer_state == k_wd_fdc_timer_none);

  /* The reset line doesn't seem to affect the track or data registers. */
  p_fdc->track_register = 0;
  p_fdc->data_register = 0;
}

static void
wd_fdc_set_drq(struct wd_fdc_struct* p_fdc, int level) {
  p_fdc->is_drq = level;
  if (level) {
    if (p_fdc->status_register & k_wd_fdc_status_type_II_III_drq) {
      p_fdc->status_register |= k_wd_fdc_status_type_II_III_lost_byte;
    }
    p_fdc->status_register |= k_wd_fdc_status_type_II_III_drq;
  } else {
    p_fdc->status_register &= ~k_wd_fdc_status_type_II_III_drq;
  }
  wd_fdc_update_nmi(p_fdc);
}

static void
wd_fdc_do_command(struct wd_fdc_struct* p_fdc, uint8_t val) {
  uint8_t command;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  uint32_t step_rate_ms = 0;

  if (p_fdc->log_commands) {
    int32_t track = -1;
    int32_t head_pos = -1;
    if (p_current_drive != NULL) {
      track = disc_drive_get_track(p_current_drive);
      head_pos = disc_drive_get_head_position(p_current_drive);
    }
    log_do_log(k_log_disc,
               k_log_info,
               "1770: command $%.2X tr %d sr %d dr %d cr $%.2X ptrk %d hpos %d",
               val,
               p_fdc->track_register,
               p_fdc->sector_register,
               p_fdc->data_register,
               p_fdc->control_register,
               track,
               head_pos);
  }

  assert(!wd_fdc_is_reset(p_fdc->control_register));

  if (p_fdc->p_current_drive == NULL) {
    util_bail("command while no selected drive");
  }

  command = (val & 0xF0);

  if (command == k_wd_fdc_command_force_interrupt) {
    uint8_t force_interrupt_bits = (val & 0x0F);
    /* EMU NOTE: force interrupt is pretty unclear on the datasheet. From
     * testing on a real 1772:
     * - The command is aborted right away in all cases.
     * - The command completion INTRQ / NMI is _inhibited_ for $D0. In
     * particular, Watford Electronics DDFS will be unhappy unless you behave
     * correctly here.
     * - Force interrupt will spin up the motor and enter an idle state if
     * the motor is off. The idle state behaves a little like a type 1 command
     * insofar as index pulse appears to be reported in the status register.
     * - Interrupt on index pulse is only active for the current command.
     */
    if (p_fdc->status_register & k_wd_fdc_status_busy) {
      wd_fdc_command_done(p_fdc, 0);
    } else {
      assert(p_fdc->state == k_wd_fdc_state_idle);
      p_fdc->index_pulse_count = 0;
      p_fdc->command_type = 1;
      p_fdc->status_register &= k_wd_fdc_status_motor_on;
      if (!(p_fdc->status_register & k_wd_fdc_status_motor_on)) {
        p_fdc->status_register |= k_wd_fdc_status_motor_on;
        disc_drive_start_spinning(p_current_drive);
      }
    }
    if (force_interrupt_bits == 0) {
      p_fdc->is_interrupt_on_index_pulse = 0;
    } else if (force_interrupt_bits == 4) {
      p_fdc->is_interrupt_on_index_pulse = 1;
    } else {
      util_bail("1770 force interrupt flags not handled");
    }
    return;
  }

  /* EMU NOTE: this is a very murky area. There does not appear to be a simple
   * rule here. Whether a command will do anything when busy seems to depend on
   * the current command, the new command and also the current place in the
   * internal state machine!
   */
  if (p_fdc->status_register & k_wd_fdc_status_busy) {
    log_do_log(k_log_disc,
               k_log_warning,
               "1770: command $%.2X while busy with $%.2X, ignoring",
               val,
               p_fdc->command);
    return;
  }

  p_fdc->command = command;
  p_fdc->is_command_settle = 0;
  p_fdc->is_command_write = 0;
  p_fdc->is_command_verify = 0;
  p_fdc->is_command_multi = 0;
  p_fdc->is_command_deleted = 0;
  p_fdc->is_interrupt_on_index_pulse = 0;
  p_fdc->is_write_track_crc_second_byte = 0;

  switch (command) {
  case k_wd_fdc_command_restore:
  case k_wd_fdc_command_seek:
  case k_wd_fdc_command_step_in_with_update:
  case k_wd_fdc_command_step_out_with_update:
    p_fdc->command_type = 1;
    p_fdc->is_command_verify = !!(val & k_wd_fdc_command_bit_type_I_verify);
    switch (val & 0x03) {
    case 0:
      step_rate_ms = 6;
      break;
    case 1:
      step_rate_ms = 12;
      break;
    case 2:
      if (p_fdc->is_1772) {
        step_rate_ms = 2;
      } else {
        step_rate_ms = 20;
      }
      break;
    case 3:
      if (p_fdc->is_1772) {
        step_rate_ms = 3;
      } else {
        step_rate_ms = 30;
      }
      break;
    }
    p_fdc->command_step_rate_ms = step_rate_ms;
    break;
  case k_wd_fdc_command_read_sector:
  case k_wd_fdc_command_read_sector_multi:
  case k_wd_fdc_command_write_sector:
  case k_wd_fdc_command_write_sector_multi:
    p_fdc->command_type = 2;
    p_fdc->is_command_multi = !!(val & k_wd_fdc_command_bit_type_II_multi);
    break;
  case k_wd_fdc_command_read_address:
  case k_wd_fdc_command_read_track:
  case k_wd_fdc_command_write_track:
    p_fdc->command_type = 3;
    break;
  default:
    util_bail("unimplemented command $%X", val);
    break;
  }
  if (((p_fdc->command_type == 2) || (p_fdc->command_type == 3)) &&
      (val & k_wd_fdc_command_bit_type_II_III_settle)) {
    p_fdc->is_command_settle = 1;
  }
  if ((p_fdc->command == k_wd_fdc_command_write_sector) ||
      (p_fdc->command == k_wd_fdc_command_write_sector_multi) ||
      (p_fdc->command == k_wd_fdc_command_write_track)) {
    p_fdc->is_command_write = 1;
    p_fdc->is_command_deleted = (val & k_wd_fdc_command_bit_type_II_deleted);
  }

  /* All commands except force interrupt (handled above):
   * - Clear INTRQ and DRQ.
   * - Clear status register result bits.
   * - Set busy.
   * - Spin up if necessary and not inhibited.
   */
  wd_fdc_set_drq(p_fdc, 0);
  wd_fdc_set_intrq(p_fdc, 0);
  p_fdc->status_register &= k_wd_fdc_status_motor_on;
  p_fdc->status_register |= k_wd_fdc_status_busy;

  p_fdc->index_pulse_count = 0;
  if (p_fdc->status_register & k_wd_fdc_status_motor_on) {
    /* Short circuit spin-up if motor is on. */
    wd_fdc_dispatch_command(p_fdc);
  } else {
    p_fdc->status_register |= k_wd_fdc_status_motor_on;
    disc_drive_start_spinning(p_current_drive);
    /* Short circuit spin-up if command requests it. */
    /* NOTE: disabling spin-up wait is a strange facility. It makes a lot of
     * sense for a seek because the disc head can usefully get moving while the
     * motor is spinning up. But other commands like a read track also seem to
     * start immediately. It is unclear whether such a command would be
     * unreliable on a drive that takes a while to come up to speed.
     */
    if (val & k_wd_fdc_command_bit_disable_spin_up) {
      p_fdc->index_pulse_count = 6;
      log_do_log(k_log_disc,
                 k_log_warning,
                 "1770: command $%.2X spin up wait disabled, motor was off",
                 val);
      wd_fdc_dispatch_command(p_fdc);
    } else {
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_spin_up_wait);
    }
  }
}

static uint16_t
wd_fdc_opus_remap_addr(uint16_t addr) {
  if (addr < 4) {
    return (4 + addr);
  } else {
    return (addr - 4);
  }
}

static uint8_t
wd_fdc_opus_remap_val(uint16_t addr, uint16_t val) {
  uint8_t new_val;

  /* Only remap control register values. */
  if (addr >= 4) {
    return val;
  }

  new_val = k_wd_fdc_control_reset;
  if (val & 0x01) {
    new_val |= k_wd_fdc_control_drive_1;
  } else {
    new_val |= k_wd_fdc_control_drive_0;
  }
  if (val & 0x02) {
    new_val |= k_wd_fdc_control_side;
  }
  if (!(val & 0x40)) {
    new_val |= k_wd_fdc_control_density;
  }

  return new_val;
}

static uint8_t
wd_fdc_master_remap_val(uint16_t addr, uint8_t val) {
  uint8_t new_val;

  /* Only remap control register values. */
  if (addr >= 4) {
    return val;
  }

  new_val = 0;
  if (val & 0x04) {
    new_val |= k_wd_fdc_control_reset;
  }
  if (val & 0x01) {
    new_val |= k_wd_fdc_control_drive_0;
  }
  if (val & 0x02) {
    new_val |= k_wd_fdc_control_drive_1;
  }
  if (val & 0x10) {
    new_val |= k_wd_fdc_control_side;
  }
  if (val & 0x20) {
    new_val |= k_wd_fdc_control_density;
  }

  return new_val;
}

uint8_t
wd_fdc_read(struct wd_fdc_struct* p_fdc, uint16_t addr) {
  uint8_t ret = 0;

  if (p_fdc->is_opus) {
    addr = wd_fdc_opus_remap_addr(addr);
  }

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
    if ((p_fdc->command_type == 2) || (p_fdc->command_type == 3)) {
      wd_fdc_set_drq(p_fdc, 0);
    }
    ret = p_fdc->data_register;
    break;
  case 0:
  case 1:
  case 2:
  case 3:
    /* Control register isn't readable. */
    ret = 0xFE;
    break;
  default:
    assert(0);
    break;
  }

  return ret;
}

void
wd_fdc_write(struct wd_fdc_struct* p_fdc, uint16_t addr, uint8_t val) {
  if (p_fdc->is_master) {
    val = wd_fdc_master_remap_val(addr, val);
  } else if (p_fdc->is_opus) {
    addr = wd_fdc_opus_remap_addr(addr);
    val = wd_fdc_opus_remap_val(addr, val);
  }

  switch (addr) {
  case 0:
  case 1:
  case 2:
  case 3:
    if (p_fdc->log_commands) {
      log_do_log(k_log_disc,
                 k_log_info,
                 "1770: control register now $%.2X",
                 val);
    }
    if ((p_fdc->status_register & k_wd_fdc_status_busy) &&
        !wd_fdc_is_reset(val)) {
      util_bail("control register updated while busy, without reset");
    }
    wd_fdc_write_control(p_fdc, val);
    break;
  case 4:
    if (wd_fdc_is_reset(p_fdc->control_register)) {
      /* Ignore commands when the reset line is active. */
      break;
    }
    wd_fdc_do_command(p_fdc, val);
    break;
  case 5:
    p_fdc->track_register = val;
    break;
  case 6:
    if (wd_fdc_is_reset(p_fdc->control_register)) {
      /* Ignore sector register changes when the reset line is active. */
      /* EMU NOTE: track / data register changes seem to still be accepted! */
      break;
    }
    p_fdc->sector_register = val;
    break;
  case 7:
    if ((p_fdc->command_type == 2) || (p_fdc->command_type == 3)) {
      wd_fdc_set_drq(p_fdc, 0);
    }
    p_fdc->data_register = val;
    break;
  default:
    assert(0);
    break;
  }
}

static void
wd_fdc_send_data_to_host(struct wd_fdc_struct* p_fdc, uint8_t data) {
  assert((p_fdc->command_type == 2) || (p_fdc->command_type == 3));
  wd_fdc_set_drq(p_fdc, 1);
  p_fdc->data_register = data;
}

static void
wd_fdc_byte_received(struct wd_fdc_struct* p_fdc,
                     int is_index_pulse_positive_edge) {
  int is_crc_error;
  uint8_t command_type = p_fdc->command_type;
  int is_read_address = (p_fdc->command == k_wd_fdc_command_read_address);
  uint8_t data = p_fdc->deliver_data;
  int is_marker = p_fdc->deliver_is_marker;
  int is_mfm = wd_fdc_is_double_density(p_fdc->control_register);

  p_fdc->deliver_is_marker = 0;

  switch (p_fdc->state) {
  case k_wd_fdc_state_search_id:
    if (!is_marker || (data != k_ibm_disc_id_mark_data_pattern)) {
      break;
    }
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_in_id);
    p_fdc->crc = ibm_disc_format_crc_init(is_mfm);
    p_fdc->crc =
          ibm_disc_format_crc_add_byte(p_fdc->crc,
                                       k_ibm_disc_id_mark_data_pattern);
    break;
  case k_wd_fdc_state_in_id:
    switch (p_fdc->state_count) {
    case 0:
      p_fdc->on_disc_track = data;
      if (is_read_address) {
        /* The datasheet says, "The Track Address of the ID field is written
         * into the Sector register"
         */
        p_fdc->sector_register = data;
      }
      break;
    case 2:
      p_fdc->on_disc_sector = data;
      break;
    case 3:
      /* From http://info-coach.fr/atari/documents/_mydoc/WD1772-JLG.pdf,
       * only the lower two bits affect anything.
       */
      p_fdc->on_disc_length = (128 << (data & 0x03));
      break;
    default:
      break;
    }
    if (is_read_address) {
      /* Note that unlike the 8271, the CRC bytes are sent along too. */
      wd_fdc_send_data_to_host(p_fdc, data);
    }
    if (p_fdc->state_count < 4) {
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
    } else {
      p_fdc->on_disc_crc <<= 8;
      p_fdc->on_disc_crc |= data;
    }
    p_fdc->state_count++;
    if (p_fdc->state_count != 6) {
      break;
    }

    is_crc_error = (p_fdc->crc != p_fdc->on_disc_crc);

    if (is_read_address) {
      if (is_crc_error) {
        p_fdc->status_register |= k_wd_fdc_status_crc_error;
      }
      /* Unlike the 8271, read address returns just a single record. It is also
       * not synronized to the index pulse.
       */
      /* EMU TODO: it's likely that timing is generally off for most states,
       * i.e. the 1770 takes various numbers of internal clock cycles before it
       * delivers the CRC error, before it goes not busy, etc.
       */
      /* EMU NOTE: must not clear busy flag right away. The 1770 delivers the
       * last header byte DRQ separately from lowering the busy flag.
       */
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_done);
      break;
    }

    /* The data sheet specifies no CRC error unless the fields match so check
     * those first.
     */
    if (p_fdc->track_register != p_fdc->on_disc_track) {
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_id);
      break;
    }
    if ((command_type == 2) &&
        (p_fdc->sector_register != p_fdc->on_disc_sector)) {
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_id);
      break;
    }
    if (is_crc_error) {
      p_fdc->status_register |= k_wd_fdc_status_crc_error;
      /* Unlike the 8271, the 1770 keeps going. */
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_id);
      break;
    }
    if (command_type == 1) {
      wd_fdc_command_done(p_fdc, 1);
    } else if (p_fdc->is_command_write) {
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_write_sector_delay);
    } else {
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_data);
    }
    break;
  case k_wd_fdc_state_search_data:
    p_fdc->state_count++;
    /* Like the 8271, the data mark is only recognized if 14 bytes have passed.
     * Unlike the 8271, it gives up after a while longer.
     */
    if (p_fdc->state_count <= (14 * ((uint32_t) is_mfm + 1))) {
      break;
    }
    if (p_fdc->state_count >= (31 * ((uint32_t) is_mfm + 1))) {
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_id);
      break;
    }
    if (!is_marker) {
      break;
    }
    if (data == k_ibm_disc_data_mark_data_pattern) {
      /* Nothing. */
    } else if (data == k_ibm_disc_deleted_data_mark_data_pattern) {
      /* EMU NOTE: the datasheet is ambiguous on whether the deleted mark is
       * visible in the status register immediately, or at the end of a read.
       * The state machine diagram says "DAM in time" -> "Set Record Type in
       * Status Bit 5". But later on it says "At the end of the Read... is
       * recorded...".
       * Testing on my 1772, the state machine diagram is correct: the bit is
       * visible in the status register immediately during the read.
       */
      /* EMU NOTE: on a multi-sector read, the deleted mark bit is set, and left
       * set, if _any_ deleted data sector was encountered. The datasheet would
       * seem to imply that only the most recent sector type is reflected in
       * the bit, but testing on my 1772, the bit is set and left set even if
       * a non-deleted sector is encountered subsequently.
       */
      p_fdc->status_register |= k_wd_fdc_status_type_II_III_deleted_mark;
    } else {
      break;
    }

    wd_fdc_set_state(p_fdc, k_wd_fdc_state_in_data);
    /* CRC error is reset here. It's possible to hit a CRC error in a sector
     * header and then find an ok matching sector header.
     */
    p_fdc->status_register &= ~k_wd_fdc_status_crc_error;
    p_fdc->crc = ibm_disc_format_crc_init(is_mfm);
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
    break;
  case k_wd_fdc_state_in_data:
    p_fdc->state_count++;
    if (p_fdc->state_count <= p_fdc->on_disc_length) {
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
      wd_fdc_send_data_to_host(p_fdc, data);
      break;
    } else if (p_fdc->state_count <= (p_fdc->on_disc_length + 2)) {
      p_fdc->on_disc_crc <<= 8;
      p_fdc->on_disc_crc |= data;
      break;
    }
    if (p_fdc->crc != p_fdc->on_disc_crc) {
      p_fdc->status_register |= k_wd_fdc_status_crc_error;
      /* Sector data CRC error is terminal, even for a multi-sector read. */
      wd_fdc_command_done(p_fdc, 1);
      break;
    }
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_check_multi);
    break;
  case k_wd_fdc_state_in_read_track:
    if (!is_index_pulse_positive_edge) {
      wd_fdc_send_data_to_host(p_fdc, data);
      break;
    }
    wd_fdc_command_done(p_fdc, 1);
    break;
  default:
    assert(0);
    break;
  }
}

static int
wd_fdc_mark_detector_triggered(struct wd_fdc_struct* p_fdc) {
  uint64_t mark_detector = p_fdc->mark_detector;

  if (wd_fdc_is_double_density(p_fdc->control_register)) {
    /* EMU NOTE: unsure as to exactly when MFM sync bytes are spotted. Here we
     * look for MFM 0x00 then MFM 0xA1 (sync).
     * The documented sequence is 12x 0x00, 3x 0xA1 (sync).
     */
    if ((mark_detector & 0x00000000FFFFFFFFull) == 0x00000000AAAA4489ull) {
      p_fdc->deliver_data = 0xA1;
      return 1;
    }
    /* TODO: sync to C2 (5224).
     * Note that an early, naive attempt had it triggering in the middle of
     * the sector data, so we'll need to study how it actually works in
     * detail.
     */
    /* Tag the byte after 3 sync bytes as a marker. */
    if ((mark_detector & 0xFFFFFFFFFFFF0000ull) == 0x4489448944890000ull) {
      p_fdc->deliver_is_marker = 1;
    }
  } else {
    /* The FM mark detector appears to need 4 data bits' worth of 0, with clock
     * bits set to 1, to be able to trigger.
     * Tried on my real 1772 based machine.
     */
    if ((mark_detector & 0x0000FFFF00000000ull) == 0x0000888800000000ull) {
      uint8_t clocks;
      uint8_t data;
      int is_iffy_pulse;
      ibm_disc_format_2us_pulses_to_fm(&clocks,
                                       &data,
                                       &is_iffy_pulse,
                                       mark_detector);
      if (!is_iffy_pulse && (clocks == 0xC7)) {
        /* TODO: see http://info-coach.fr/atari/documents/_mydoc/WD1772-JLG.pdf
         * This suggests that a wider ranges of byte values will function as
         * markers. It may also differ FM vs. MFM.
         */
        if ((data == 0xF8) || (data == 0xFB) || (data == 0xFE)) {
          /* Resync to marker. */
          p_fdc->deliver_data = data;
          p_fdc->deliver_is_marker = 1;
          return 1;
        }
      }
    }
  }

  return 0;
}

static void
wd_fdc_bit_received(struct wd_fdc_struct* p_fdc, int bit) {
  uint32_t data_shifter;

  /* Always run the mark detector. For a command like "read track", the 1770
   * will re-sync in the middle of the command as appropriate.
   */
  p_fdc->mark_detector <<= 1;
  p_fdc->mark_detector |= bit;

  if (wd_fdc_mark_detector_triggered(p_fdc)) {
    p_fdc->data_shifter = 0;
    p_fdc->data_shift_count = 0;
    return;
  }

  data_shifter = p_fdc->data_shifter;
  data_shifter <<= 1;
  data_shifter |= bit;
  p_fdc->data_shifter = data_shifter;
  p_fdc->data_shift_count++;

  if (wd_fdc_is_double_density(p_fdc->control_register)) {
    if (p_fdc->data_shift_count == 16) {
      p_fdc->deliver_data = ibm_disc_format_2us_pulses_to_mfm(data_shifter);
      p_fdc->data_shifter = 0;
      p_fdc->data_shift_count = 0;
    }
  } else {
    if (p_fdc->data_shift_count == 32) {
      uint8_t unused_clocks;
      int is_iffy_pulse;
      ibm_disc_format_2us_pulses_to_fm(&unused_clocks,
                                       &p_fdc->deliver_data,
                                       &is_iffy_pulse,
                                       data_shifter);
      /* If we're reading MFM as FM, the pulses won't all fall on 4us
       * boundaries. This is fuzzy bits. We'll return a non-stable read.
       */
      if (is_iffy_pulse) {
        struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
        data_shifter = disc_drive_get_quasi_random_pulses(p_current_drive);
        ibm_disc_format_2us_pulses_to_fm(&unused_clocks,
                                         &p_fdc->deliver_data,
                                         &is_iffy_pulse,
                                         data_shifter);
      }
      p_fdc->data_shifter = 0;
      p_fdc->data_shift_count = 0;
    }
  }
}

static void
wd_fdc_bitstream_received(struct wd_fdc_struct* p_fdc,
                          uint32_t pulses,
                          uint32_t pulses_count,
                          int is_index_pulse_positive_edge) {
  uint32_t i;

  pulses <<= (32 - pulses_count);

  for (i = 0; i < pulses_count; ++i) {
    int bit = !!(pulses & 0x80000000);
    wd_fdc_bit_received(p_fdc, bit);
    pulses <<= 1;
  }
  wd_fdc_byte_received(p_fdc, is_index_pulse_positive_edge);
}

static void
wd_write_byte(struct wd_fdc_struct* p_fdc,
              int is_mfm,
              uint8_t byte,
              int is_marker) {
  uint32_t pulses = 0;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;

  if (is_mfm) {
    if (is_marker) {
      switch (byte) {
      case 0xA1:
        /* The famous 0x4489. */
        pulses = k_ibm_disc_mfm_a1_sync;
        break;
      case 0xC2:
        pulses = k_ibm_disc_mfm_c2_sync;
        break;
      default:
        assert(0);
        break;
      }
    } else {
      pulses = ibm_disc_format_mfm_to_2us_pulses(&p_fdc->last_mfm_bit, byte);
    }
  } else {
    uint8_t clocks = 0xFF;
    if (is_marker) {
      switch (byte) {
      case 0xFC:
        clocks = 0xD7;
        break;
      case 0xF8:
      case 0xF9:
      case 0xFA:
      case 0xFB:
      case 0xFE:
        clocks = k_ibm_disc_mark_clock_pattern;
        break;
      default:
        assert(0);
        break;
      }
    }
    pulses = ibm_disc_format_fm_to_2us_pulses(clocks, byte);
  }

  disc_drive_write_pulses(p_current_drive, pulses);
}

static void
wd_fdc_pulses_callback(void* p, uint32_t pulses, uint32_t count) {
  /* NOTE: this callback routine is also used for seek / settle timing,
   * which is not a precise 64us basis.
   */
  int is_index_pulse;
  int is_marker;
  int is_preset_crc;
  uint8_t data_byte;

  struct wd_fdc_struct* p_fdc = (struct wd_fdc_struct*) p;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  int was_index_pulse = p_fdc->is_index_pulse;
  int is_index_pulse_positive_edge = 0;
  int is_mfm = (count == 16);

  assert(p_current_drive != NULL);
  assert(disc_drive_is_spinning(p_current_drive));
  assert(p_fdc->status_register & k_wd_fdc_status_motor_on);

  is_index_pulse = disc_drive_is_index_pulse(p_fdc->p_current_drive);
  p_fdc->is_index_pulse = is_index_pulse;
  if (is_index_pulse && !was_index_pulse) {
    is_index_pulse_positive_edge = 1;
  }

  if (p_fdc->is_interrupt_on_index_pulse && is_index_pulse_positive_edge) {
    wd_fdc_set_intrq(p_fdc, 1);
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
    assert(p_fdc->index_pulse_count <= 10);
    if (p_fdc->index_pulse_count == 10) {
      if (p_fdc->log_commands) {
        log_do_log(k_log_disc, k_log_info, "1770: automatic motor off");
      }
      disc_drive_stop_spinning(p_current_drive);
      p_fdc->status_register &= ~k_wd_fdc_status_motor_on;
      /* EMU NOTE: in my testing on a 1772, the polled type 1 status bits get
       * cleared on spindown.
       */
      if (p_fdc->command_type == 1) {
        p_fdc->status_register &=
            ~(k_wd_fdc_status_type_I_track_0 | k_wd_fdc_status_type_I_index);
      }
    }
    break;
  case k_wd_fdc_state_timer_wait:
    break;
  case k_wd_fdc_state_spin_up_wait:
    assert(p_fdc->index_pulse_count <= 6);
    if (p_fdc->index_pulse_count != 6) {
      break;
    }
    if (p_fdc->command_type == 1) {
      p_fdc->status_register |= k_wd_fdc_status_type_I_spin_up_done;
    }
    if (p_fdc->is_command_settle) {
      uint32_t settle_ms = k_wd_fdc_1770_settle_ms;
      if (p_fdc->is_1772) {
        settle_ms /= 2;
      }
      wd_fdc_start_timer(p_fdc, k_wd_fdc_timer_settle, (settle_ms * 1000));
    } else {
      wd_fdc_dispatch_command(p_fdc);
    }
    break;
  case k_wd_fdc_state_wait_index:
    if (!is_index_pulse_positive_edge) {
      break;
    }
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_in_read_track);
    /* EMU NOTE: Need to include this byte (directly after the index pulse
     * positive edge) in the read track data. Confirmed with a real 1772 +
     * Gotek.
     */
    wd_fdc_bitstream_received(p_fdc, pulses, count, 0);
    break;
  case k_wd_fdc_state_search_id:
  case k_wd_fdc_state_in_id:
  case k_wd_fdc_state_search_data:
  case k_wd_fdc_state_in_data:
  case k_wd_fdc_state_in_read_track:
    wd_fdc_bitstream_received(p_fdc,
                              pulses,
                              count,
                              is_index_pulse_positive_edge);

    assert(p_fdc->index_pulse_count <= 6);
    if (p_fdc->index_pulse_count == 6) {
      p_fdc->status_register |= k_wd_fdc_status_record_not_found;
      wd_fdc_command_done(p_fdc, 1);
    }
    break;
  case k_wd_fdc_state_write_sector_delay:
    /* Following the data sheet here for byte-by-byte behavior. */
    if (p_fdc->state_count == 0) {
      p_fdc->index_pulse_count = 0;
    } else if (p_fdc->state_count == 1) {
      wd_fdc_set_drq(p_fdc, 1);
    } else if ((p_fdc->state_count == 10) &&
               p_fdc->status_register & k_wd_fdc_status_type_II_III_drq) {
      p_fdc->status_register |= k_wd_fdc_status_type_II_III_lost_byte;
      wd_fdc_command_done(p_fdc, 1);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == 12) {
      if (is_mfm) {
        wd_fdc_set_state(p_fdc, k_wd_fdc_state_write_sector_lead_in_mfm);
      } else {
        wd_fdc_set_state(p_fdc, k_wd_fdc_state_write_sector_lead_in_fm);
      }
    }
    break;
  case k_wd_fdc_state_write_sector_lead_in_fm:
    wd_write_byte(p_fdc, is_mfm, 0x00, 0);
    p_fdc->state_count++;
    if (p_fdc->state_count == 6) {
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_write_sector_marker_fm);
    }
    break;
  case k_wd_fdc_state_write_sector_lead_in_mfm:
    if (p_fdc->state_count < 11) {
      /* Nothing. */
    } else {
      wd_write_byte(p_fdc, is_mfm, 0x00, 0);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == 23) {
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_write_sector_marker_mfm);
    }
    break;
  case k_wd_fdc_state_write_sector_marker_fm:
    data_byte = k_ibm_disc_data_mark_data_pattern;
    if (p_fdc->is_command_deleted) {
      data_byte = k_ibm_disc_deleted_data_mark_data_pattern;
    }
    p_fdc->crc = ibm_disc_format_crc_init(0);
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
    wd_write_byte(p_fdc, 0, data_byte, 1);
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_write_sector_body);
    break;
  case k_wd_fdc_state_write_sector_marker_mfm:
    if (p_fdc->state_count < 3) {
      wd_write_byte(p_fdc, 1, 0xA1, 1);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == 4) {
      data_byte = k_ibm_disc_data_mark_data_pattern;
      if (p_fdc->is_command_deleted) {
        data_byte = k_ibm_disc_deleted_data_mark_data_pattern;
      }
      p_fdc->crc = ibm_disc_format_crc_init(1);
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
      wd_write_byte(p_fdc, 1, data_byte, 0);
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_write_sector_body);
    }
    break;
  case k_wd_fdc_state_write_sector_body:
    if (p_fdc->state_count < p_fdc->on_disc_length) {
      data_byte = p_fdc->data_register;
      if (p_fdc->status_register & k_wd_fdc_status_type_II_III_drq) {
        data_byte = 0;
        p_fdc->status_register |= k_wd_fdc_status_type_II_III_lost_byte;
        /* EMU NOTE: doesn't terminate command, like it would on the 8271. */
      }
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
      wd_write_byte(p_fdc, is_mfm, data_byte, 0);
      if (p_fdc->state_count != (p_fdc->on_disc_length - 1)) {
        wd_fdc_set_drq(p_fdc, 1);
      }
    } else if (p_fdc->state_count < (p_fdc->on_disc_length + 2)) {
      wd_write_byte(p_fdc, is_mfm, (p_fdc->crc >> 8), 0);
      p_fdc->crc <<= 8;
    } else {
      wd_write_byte(p_fdc, is_mfm, 0xFF, 0);
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_check_multi);
    }
    p_fdc->state_count++;
    break;
  case k_wd_fdc_state_check_multi:
    if (p_fdc->is_command_multi) {
      p_fdc->sector_register++;
      p_fdc->index_pulse_count = 0;
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_id);
    } else {
      wd_fdc_command_done(p_fdc, 1);
    }
    break;
  case k_wd_fdc_state_write_track_setup:
    if (p_fdc->state_count == 0) {
      p_fdc->index_pulse_count = 0;
      wd_fdc_set_drq(p_fdc, 1);
    } else if (p_fdc->state_count == 3) {
      if (p_fdc->status_register & k_wd_fdc_status_type_II_III_drq) {
        p_fdc->status_register |= k_wd_fdc_status_type_II_III_lost_byte;
        wd_fdc_command_done(p_fdc, 1);
      } else {
        wd_fdc_set_state(p_fdc, k_wd_fdc_state_in_write_track);
        break;
      }
    }
    p_fdc->state_count++;
    break;
  case k_wd_fdc_state_in_write_track:
    if ((p_fdc->state_count == 0) && !is_index_pulse_positive_edge) {
      break;
    }
    if (is_index_pulse_positive_edge && (p_fdc->state_count > 0)) {
      wd_fdc_command_done(p_fdc, 1);
      break;
    }
    if (p_fdc->is_write_track_crc_second_byte) {
      wd_write_byte(p_fdc, is_mfm, (p_fdc->crc & 0xFF), 0);
      p_fdc->is_write_track_crc_second_byte = 0;
      wd_fdc_set_drq(p_fdc, 1);
      break;
    }
    data_byte = p_fdc->data_register;
    if (p_fdc->status_register & k_wd_fdc_status_type_II_III_drq) {
      data_byte = 0;
      p_fdc->status_register |= k_wd_fdc_status_type_II_III_lost_byte;
    }
    is_marker = 0;
    is_preset_crc = 0;
    switch (data_byte) {
    /* 0xF5 and 0xF6 are documented as "not allowed" in FM mode. They
     * actually write 0xA1 / 0xC2 respectively, as per MFM, but it's not
     * known whether any clock bits are omitted, or whether CRC is preset,
     * so bailing for now rather than guesing.
     */
    case 0xF5:
      if (is_mfm) {
        is_marker = 1;
        is_preset_crc = 1;
        data_byte = 0xA1;
      } else {
        util_bail("not allowed FM byte");
      }
      break;
    case 0xF6:
      if (is_mfm) {
        is_marker = 1;
        data_byte = 0xC2;
      } else {
        util_bail("not allowed FM byte");
      }
      break;
    case 0xF8:
    case 0xF9:
    case 0xFA:
    case 0xFB:
    case 0xFE:
      if (!is_mfm) {
        is_marker = 1;
        is_preset_crc = 1;
      }
      break;
    case 0xFC:
      if (!is_mfm) {
        is_marker = 1;
      }
      break;
    default:
      break;
    }
    if (is_preset_crc) {
      p_fdc->crc = ibm_disc_format_crc_init(is_mfm);
    }
    if (data_byte == 0xF7) {
      wd_write_byte(p_fdc, is_mfm, (p_fdc->crc >> 8), 0);
      p_fdc->is_write_track_crc_second_byte = 1;
    } else {
      wd_write_byte(p_fdc, is_mfm, data_byte, is_marker);
      if (is_mfm && is_preset_crc) {
        /* Nothing. */
      } else {
        p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
      }
      wd_fdc_set_drq(p_fdc, 1);
    }
    p_fdc->state_count++;
    break;
  case k_wd_fdc_state_done:
    wd_fdc_command_done(p_fdc, 1);
    break;
  default:
    assert(0);
    break;
  }

  if (is_index_pulse_positive_edge) {
    p_fdc->index_pulse_count++;
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

  disc_drive_set_pulses_callback(p_drive_0, wd_fdc_pulses_callback, p_fdc);
  disc_drive_set_pulses_callback(p_drive_1, wd_fdc_pulses_callback, p_fdc);
}

void
wd_fdc_set_is_opus(struct wd_fdc_struct* p_fdc, int is_opus) {
  p_fdc->is_opus = is_opus;
}
