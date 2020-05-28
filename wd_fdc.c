#include "wd_fdc.h"

#include "bbc_options.h"
#include "disc_drive.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "state_6502.h"
#include "util.h"

#include <assert.h>

static const uint32_t k_wd_fdc_1770_settle_ticks = ((30 * 1000) / 64);

enum {
  k_wd_fdc_command_restore = 0x00,
  k_wd_fdc_command_seek = 0x10,
  k_wd_fdc_command_step_in_with_update = 0x50,
  k_wd_fdc_command_step_out_with_update = 0x70,
  k_wd_fdc_command_read_sector = 0x80,
  k_wd_fdc_command_read_sector_multi = 0x90,
  k_wd_fdc_command_write_sector = 0xA0,
  k_wd_fdc_command_read_address = 0xC0,
  k_wd_fdc_command_force_interrupt = 0xD0,
  k_wd_fdc_command_read_track = 0xE0,
};

enum {
  k_wd_fdc_command_bit_disable_spin_up = 0x08,
  k_wd_fdc_command_bit_type_I_verify = 0x04,
  k_wd_fdc_command_bit_type_II_III_settle = 0x04,
  k_wd_fdc_command_bit_type_II_multi = 0x10,
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
  k_wd_fdc_state_spin_up_wait,
  k_wd_fdc_state_settle,
  k_wd_fdc_state_settle_wait,
  k_wd_fdc_state_seek_step,
  k_wd_fdc_state_seek_step_wait,
  k_wd_fdc_state_seek_step_once,
  k_wd_fdc_state_seek_step_once_wait,
  k_wd_fdc_state_seek_check_verify,
  k_wd_fdc_state_wait_index,
  k_wd_fdc_state_search_id,
  k_wd_fdc_state_in_id,
  k_wd_fdc_state_search_data,
  k_wd_fdc_state_in_data,
  k_wd_fdc_state_in_read_track,
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
  int is_intrq;
  int is_drq;

  struct disc_drive_struct* p_current_drive;
  int is_index_pulse;
  int is_interrupt_on_index_pulse;
  uint8_t command;
  uint8_t command_type;
  int is_command_settle;
  int is_command_write;
  int is_command_verify;
  int is_command_multi;
  uint32_t command_step_ticks;
  uint32_t state;
  uint32_t state_count;
  uint32_t index_pulse_count;
  uint32_t mark_detector;
  uint16_t data_shifter;
  uint32_t data_shift_count;
  uint8_t deliver_clocks;
  uint8_t deliver_data;
  uint16_t crc;
  uint8_t on_disc_track;
  uint8_t on_disc_sector;
  uint32_t on_disc_length;
  uint16_t on_disc_crc;
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

static void
wd_fdc_set_state(struct wd_fdc_struct* p_fdc, int state) {
  p_fdc->state = state;
  p_fdc->state_count = 0;
}

static void
wd_fdc_update_nmi(struct wd_fdc_struct* p_fdc) {
  struct state_6502* p_state_6502 = p_fdc->p_state_6502;
  int firing = state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi);
  int level = (p_fdc->is_intrq || p_fdc->is_drq);

  if (firing && (level == 1)) {
    log_do_log(k_log_disc, k_log_error, "edge triggered NMI already high");
  }

  state_6502_set_irq_level(p_state_6502, k_state_6502_irq_nmi, level);
}

static void
wd_fdc_write_control(struct wd_fdc_struct* p_fdc, uint8_t val) {
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
    disc_drive_select_side(p_fdc->p_current_drive,
                           !!(val & k_wd_fdc_control_side));
  }

  p_fdc->control_register = val;

  /* Reset, active low. */
  if (!(p_fdc->control_register & k_wd_fdc_control_reset)) {
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_idle);
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
    p_fdc->is_interrupt_on_index_pulse = 0;
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

  /* The reset line doesn't seem to affect the track or data registers. */
  p_fdc->track_register = 0;
  p_fdc->data_register = 0;
}

static void
wd_fdc_set_intrq(struct wd_fdc_struct* p_fdc, int level) {
  p_fdc->is_intrq = level;
  wd_fdc_update_nmi(p_fdc);
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
wd_fdc_command_done(struct wd_fdc_struct* p_fdc) {
  assert(p_fdc->status_register & k_wd_fdc_status_busy);
  assert(p_fdc->state > k_wd_fdc_state_idle);

  p_fdc->status_register &= ~k_wd_fdc_status_busy;
  wd_fdc_set_state(p_fdc, k_wd_fdc_state_idle);
  p_fdc->index_pulse_count = 0;

  /* EMU NOTE: leave DRQ alone, if it is raised leave it raised. */
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
  uint32_t step_rate_ms = 0;

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

  assert(p_fdc->control_register & k_wd_fdc_control_reset);

  if (!(p_fdc->control_register & k_wd_fdc_control_density)) {
    util_bail("command while double density");
  }

  command = (val & 0xF0);

  if (command == k_wd_fdc_command_force_interrupt) {
    uint8_t force_interrupt_bits = (val & 0x0F);
    if (force_interrupt_bits == 0) {
      if (p_fdc->status_register & k_wd_fdc_status_busy) {
        wd_fdc_command_done(p_fdc);
      }
      p_fdc->is_interrupt_on_index_pulse = 0;
    } else if (force_interrupt_bits == 4) {
      p_fdc->is_interrupt_on_index_pulse = 1;
    } else {
      util_bail("1770 force interrupt flags not handled");
    }
    return;
  }

  if (p_fdc->p_current_drive == NULL) {
    util_bail("command while no selected drive");
  }
  if (p_fdc->status_register & k_wd_fdc_status_busy) {
    util_bail("command while busy");
  }

  p_fdc->command = command;
  p_fdc->is_command_settle = 0;
  p_fdc->is_command_write = 0;
  p_fdc->is_command_verify = 0;
  p_fdc->is_command_multi = 0;

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
      step_rate_ms = 20;
      break;
    case 3:
      step_rate_ms = 30;
      break;
    }
    p_fdc->command_step_ticks = ((step_rate_ms * 1000) / 64);
    break;
  case k_wd_fdc_command_read_sector:
  case k_wd_fdc_command_read_sector_multi:
  case k_wd_fdc_command_write_sector:
    p_fdc->command_type = 2;
    p_fdc->is_command_multi = !!(val & k_wd_fdc_command_bit_type_II_multi);
    break;
  case k_wd_fdc_command_read_address:
  case k_wd_fdc_command_read_track:
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
  if (p_fdc->command == k_wd_fdc_command_write_sector) {
    p_fdc->is_command_write = 1;
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

  wd_fdc_set_state(p_fdc, k_wd_fdc_state_spin_up_wait);
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
    if ((p_fdc->command_type == 2) || (p_fdc->command_type == 3)) {
      wd_fdc_set_drq(p_fdc, 0);
    }
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
    if (p_fdc->log_commands) {
      log_do_log(k_log_disc,
                 k_log_info,
                 "1770: control register now $%.2X",
                 val);
    }
    if (p_fdc->status_register & k_wd_fdc_status_busy) {
      util_bail("control register updated while busy");
    }
    wd_fdc_write_control(p_fdc, val);
    break;
  case 4:
    if (!(p_fdc->control_register & k_wd_fdc_control_reset)) {
      /* Ignore commands when the reset line is active. */
      break;
    }
    wd_fdc_do_command(p_fdc, val);
    break;
  case 5:
    p_fdc->track_register = val;
    break;
  case 6:
    if (!(p_fdc->control_register & k_wd_fdc_control_reset)) {
      /* Ignore sector register changes when the reset line is active. */
      /* EMU NOTE: track / data register changes seem to still be accepted! */
      break;
    }
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
wd_fdc_send_data_to_host(struct wd_fdc_struct* p_fdc, uint8_t data) {
  assert((p_fdc->command_type == 2) || (p_fdc->command_type == 3));
  wd_fdc_set_drq(p_fdc, 1);
  p_fdc->data_register = data;
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
wd_fdc_byte_received(struct wd_fdc_struct* p_fdc,
                     uint8_t clocks,
                     uint8_t data,
                     int is_index_pulse_positive_edge) {
  int is_crc_error;
  int is_deleted;
  uint8_t command_type = p_fdc->command_type;
  int is_read_address = (p_fdc->command == k_wd_fdc_command_read_address);

  switch (p_fdc->state) {
  case k_wd_fdc_state_search_id:
    if ((clocks != k_ibm_disc_mark_clock_pattern) ||
        (data != k_ibm_disc_id_mark_data_pattern)) {
      break;
    }
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_in_id);
    p_fdc->crc = ibm_disc_format_crc_init();
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
      wd_fdc_command_done(p_fdc);
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
      wd_fdc_command_done(p_fdc);
    } else {
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_data);
    }
    break;
  case k_wd_fdc_state_search_data:
    /* TODO: enforce minumum and maximum bytes for this state. */
    p_fdc->state_count++;
    if (clocks != k_ibm_disc_mark_clock_pattern) {
      break;
    }
    if (data == k_ibm_disc_data_mark_data_pattern) {
      is_deleted = 0;
    } else if (data == k_ibm_disc_deleted_data_mark_data_pattern) {
      /* EMU NOTE: the datasheet is ambiguous on whether the deleted mark is
       * visible in the status register immediately, or at the end of a read.
       * The state machine diagram says "DAM in time" -> "Set Record Type in
       * Status Bit 5". But later on it says "At the end of the Read... is
       * recorded...".
       * Testing on my 1772, the state machine diagram is correct: the bit is
       * visible in the status register immediately during the read.
       */
      is_deleted = 1;
    } else {
      break;
    }

    /* EMU NOTE: on a multi-sector read, we only tag the type of the most
     * recently seen data marker. I believe this is what the data sheet is
     * saying.
     * (The 8271 instead reports an accumulation of whether _any_ data marker
     * was deleted.
     */
    p_fdc->status_register &= ~k_wd_fdc_status_type_II_III_deleted_mark;
    if (is_deleted) {
      p_fdc->status_register |= k_wd_fdc_status_type_II_III_deleted_mark;
    }
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_in_data);
    /* CRC error is reset here. It's possible to hit a CRC error in a sector
     * header and then find an ok matching sector header.
     */
    p_fdc->status_register &= ~k_wd_fdc_status_crc_error;
    p_fdc->crc = ibm_disc_format_crc_init();
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
      wd_fdc_command_done(p_fdc);
      break;
    }
    if (p_fdc->is_command_multi) {
      p_fdc->sector_register++;
      p_fdc->index_pulse_count = 0;
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_id);
    } else {
      wd_fdc_command_done(p_fdc);
    }
    break;
  case k_wd_fdc_state_in_read_track:
    if (!is_index_pulse_positive_edge) {
      wd_fdc_send_data_to_host(p_fdc, data);
      break;
    }
    wd_fdc_command_done(p_fdc);
    break;
  default:
    assert(0);
    break;
  }
}

static uint8_t
wd_fdc_extract_clocks(uint16_t unseparated) {
  uint8_t ret = 0;

  if (unseparated & 0x8000) ret |= 0x80;
  if (unseparated & 0x2000) ret |= 0x40;
  if (unseparated & 0x0800) ret |= 0x20;
  if (unseparated & 0x0200) ret |= 0x10;
  if (unseparated & 0x0080) ret |= 0x08;
  if (unseparated & 0x0020) ret |= 0x04;
  if (unseparated & 0x0008) ret |= 0x02;
  if (unseparated & 0x0002) ret |= 0x01;

  return ret;
}

static uint8_t
wd_fdc_extract_data(uint16_t unseparated) {
  uint8_t ret = 0;

  if (unseparated & 0x4000) ret |= 0x80;
  if (unseparated & 0x1000) ret |= 0x40;
  if (unseparated & 0x0400) ret |= 0x20;
  if (unseparated & 0x0100) ret |= 0x10;
  if (unseparated & 0x0040) ret |= 0x08;
  if (unseparated & 0x0010) ret |= 0x04;
  if (unseparated & 0x0004) ret |= 0x02;
  if (unseparated & 0x0001) ret |= 0x01;

  return ret;
}

static void
wd_fdc_bit_received(struct wd_fdc_struct* p_fdc, int bit) {
  uint8_t clocks;
  uint8_t data;
  uint32_t mark_detector;
  uint16_t data_shifter;

  /* Always run the mark detector. For a command like "read track", the 1770
   * will re-sync in the middle of the command as appropriate.
   */
  p_fdc->mark_detector <<= 1;
  p_fdc->mark_detector |= bit;
  mark_detector = p_fdc->mark_detector;
  /* The mark detector appears to need 4 data bits' worth of 0, with 1 clock
   * bits, to be able to trigger.
   */
  if ((p_fdc->mark_detector & 0x00FF0000) == 0x00AA0000) {
    clocks = wd_fdc_extract_clocks(mark_detector & 0xFFFF);
    data = wd_fdc_extract_data(mark_detector & 0xFFFF);
    if (clocks == 0xC7) {
      if ((data == 0xF8) || (data == 0xFB) || (data == 0xFE)) {
        /* Resync to marker. */
        p_fdc->deliver_clocks = clocks;
        p_fdc->deliver_data = data;
        p_fdc->data_shift_count = 0;
        return;
      }
    }
  }

  data_shifter = p_fdc->data_shifter;
  data_shifter <<= 1;
  data_shifter |= bit;
  p_fdc->data_shifter = data_shifter;
  p_fdc->data_shift_count++;
  if (p_fdc->data_shift_count == 16) {
    clocks = wd_fdc_extract_clocks(data_shifter);
    data = wd_fdc_extract_data(data_shifter);
    p_fdc->deliver_clocks = clocks;
    p_fdc->deliver_data = data;
    p_fdc->data_shift_count = 0;
  }
}

static void
wd_fdc_bitstream_received(struct wd_fdc_struct* p_fdc,
                          uint8_t clocks_byte,
                          uint8_t data_byte,
                          int is_index_pulse_positive_edge) {
  uint32_t i;

  for (i = 0; i < 8; ++i) {
    int bit = !!(clocks_byte & 0x80);
    clocks_byte <<= 1;
    wd_fdc_bit_received(p_fdc, bit);
    bit = !!(data_byte & 0x80);
    data_byte <<= 1;
    wd_fdc_bit_received(p_fdc, bit);
  }
  wd_fdc_byte_received(p_fdc,
                       p_fdc->deliver_clocks,
                       p_fdc->deliver_data,
                       is_index_pulse_positive_edge);
}

static void
wd_fdc_byte_callback(void* p, uint8_t data_byte, uint8_t clocks_byte) {
  int state;
  int step_direction;
  int is_index_pulse;

  struct wd_fdc_struct* p_fdc = (struct wd_fdc_struct*) p;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  int was_index_pulse = p_fdc->is_index_pulse;
  int is_index_pulse_positive_edge = 0;

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
      /* EMU TODO: other bits get cleared? */
    }
    break;
  case k_wd_fdc_state_spin_up_wait:
    assert(p_fdc->index_pulse_count <= 6);
    if (p_fdc->index_pulse_count != 6) {
      break;
    }
    if (p_fdc->command_type == 1) {
      p_fdc->status_register |= k_wd_fdc_status_type_I_spin_up_done;
    }
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_settle);
    break;
  case k_wd_fdc_state_settle:
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_settle_wait);
    if (!p_fdc->is_command_settle) {
      /* Short circuit settle if not selected. */
      p_fdc->state_count = k_wd_fdc_1770_settle_ticks;
    }
    break;
  case k_wd_fdc_state_settle_wait:
    if (p_fdc->state_count != k_wd_fdc_1770_settle_ticks) {
      p_fdc->state_count++;
      break;
    }
    if (p_fdc->is_command_write) {
      p_fdc->status_register |= k_wd_fdc_status_write_protected;
      wd_fdc_command_done(p_fdc);
      break;
    }
    switch (p_fdc->command) {
    case k_wd_fdc_command_restore:
      p_fdc->track_register = 0xFF;
      p_fdc->data_register = 0;
      /* Fall through. */
    case k_wd_fdc_command_seek:
      state = k_wd_fdc_state_seek_step;
      break;
    case k_wd_fdc_command_step_in_with_update:
    case k_wd_fdc_command_step_out_with_update:
      state = k_wd_fdc_state_seek_step_once;
      break;
    case k_wd_fdc_command_read_sector:
    case k_wd_fdc_command_read_sector_multi:
    case k_wd_fdc_command_write_sector:
    case k_wd_fdc_command_read_address:
      state = k_wd_fdc_state_search_id;
      p_fdc->index_pulse_count = 0;
      break;
    case k_wd_fdc_command_read_track:
      state = k_wd_fdc_state_wait_index;
      p_fdc->index_pulse_count = 0;
      break;
    default:
      assert(0);
      break;
    }
    wd_fdc_set_state(p_fdc, state);
    break;
  case k_wd_fdc_state_seek_step:
    if (p_fdc->track_register == p_fdc->data_register) {
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_seek_check_verify);
      break;
    }
    if (p_fdc->track_register > p_fdc->data_register) {
      step_direction = -1;
    } else {
      step_direction = 1;
    }
    p_fdc->track_register += step_direction;
    if ((disc_drive_get_track(p_current_drive) == 0) &&
        (step_direction == -1)) {
      p_fdc->track_register = 0;
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_seek_check_verify);
      break;
    }
    disc_drive_seek_track(p_current_drive, step_direction);
    /* TRK0 signal may have raised or lowered. */
    wd_fdc_update_type_I_status_bits(p_fdc);
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_seek_step_wait);
    break;
  case k_wd_fdc_state_seek_step_wait:
    if (p_fdc->state_count != p_fdc->command_step_ticks) {
      p_fdc->state_count++;
      break;
    }
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_seek_step);
    break;
  case k_wd_fdc_state_seek_step_once:
    switch (p_fdc->command) {
    case k_wd_fdc_command_step_in_with_update:
      step_direction = 1;
      break;
    case k_wd_fdc_command_step_out_with_update:
      step_direction = -1;
      break;
    default:
      assert(0);
      break;
    }
    disc_drive_seek_track(p_current_drive, step_direction);
    p_fdc->track_register += step_direction;
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_seek_step_once_wait);
    break;
  case k_wd_fdc_state_seek_step_once_wait:
    if (p_fdc->state_count != p_fdc->command_step_ticks) {
      p_fdc->state_count++;
      break;
    }
    wd_fdc_set_state(p_fdc, k_wd_fdc_state_seek_check_verify);
    break;
  case k_wd_fdc_state_seek_check_verify:
    if (p_fdc->is_command_verify) {
      p_fdc->index_pulse_count = 0;
      wd_fdc_set_state(p_fdc, k_wd_fdc_state_search_id);
    } else {
      wd_fdc_command_done(p_fdc);
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
    wd_fdc_bitstream_received(p_fdc, clocks_byte, data_byte, 0);
    break;
  case k_wd_fdc_state_search_id:
  case k_wd_fdc_state_in_id:
  case k_wd_fdc_state_search_data:
  case k_wd_fdc_state_in_data:
  case k_wd_fdc_state_in_read_track:
    wd_fdc_bitstream_received(p_fdc,
                              clocks_byte,
                              data_byte,
                              is_index_pulse_positive_edge);

    assert(p_fdc->index_pulse_count <= 6);
    if (p_fdc->index_pulse_count == 6) {
      p_fdc->status_register |= k_wd_fdc_status_record_not_found;
      wd_fdc_command_done(p_fdc);
    }
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

  disc_drive_set_byte_callback(p_drive_0, wd_fdc_byte_callback, p_fdc);
  disc_drive_set_byte_callback(p_drive_1, wd_fdc_byte_callback, p_fdc);
}
