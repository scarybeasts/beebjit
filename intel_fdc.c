#include "intel_fdc.h"

#include "bbc_options.h"
#include "disc_drive.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "state_6502.h"
#include "util.h"

#include <assert.h>
#include <string.h>

/* Public command constants from the datasheet, included here for reference.
 * These are not used internally. Internally, the command is derived by
 * shifting right by 2. The remaining bits are used to describe parameter
 * counts and variants.
enum {
  k_intel_fdc_command_scan_sectors = 0x00,
  k_intel_fdc_command_scan_sectors_with_deleted = 0x04,
  k_intel_fdc_command_write_sector_128 = 0x0A,
  k_intel_fdc_command_write_sectors = 0x0B,
  k_intel_fdc_command_write_sector_deleted_128 = 0x0E,
  k_intel_fdc_command_write_sectors_deleted = 0x0F,
  k_intel_fdc_command_read_sector_128 = 0x12,
  k_intel_fdc_command_read_sectors = 0x13,
  k_intel_fdc_command_read_sector_with_deleted_128 = 0x16,
  k_intel_fdc_command_read_sectors_with_deleted = 0x17,
  k_intel_fdc_command_read_sector_ids = 0x1B,
  k_intel_fdc_command_verify_sector_128 = 0x1E,
  k_intel_fdc_command_verify_sectors = 0x1F,
  k_intel_fdc_command_format = 0x23,
  k_intel_fdc_command_seek = 0x29,
  k_intel_fdc_command_read_drive_status = 0x2C,
  k_intel_fdc_command_specify = 0x35,
  k_intel_fdc_command_write_special_register = 0x3A,
  k_intel_fdc_command_read_special_register = 0x3D,
};
*/

enum {
  /* Read. */
  k_intel_fdc_status = 0,
  k_intel_fdc_result = 1,
  k_intel_fdc_unknown_read_2 = 2,
  k_intel_fdc_unknown_read_3 = 3,

  /* Write. */
  k_intel_fdc_command = 0,
  k_intel_fdc_parameter = 1,
  k_intel_fdc_reset = 2,

  /* Read / write. */
  k_intel_fdc_data = 4,
};

enum {
  k_intel_fdc_parameter_accept_none = 1,
  k_intel_fdc_parameter_accept_command = 2,
  k_intel_fdc_parameter_accept_specify = 3,
};

enum {
  k_intel_fdc_index_pulse_none = 1,
  k_intel_fdc_index_pulse_timeout = 2,
  k_intel_fdc_index_pulse_spindown = 3,
};

enum {
  k_intel_fdc_status_flag_busy = 0x80,
  k_intel_fdc_status_flag_command_full = 0x40,
  k_intel_fdc_status_flag_param_full = 0x20,
  k_intel_fdc_status_flag_result_ready = 0x10,
  k_intel_fdc_status_flag_nmi = 0x08,
  k_intel_fdc_status_flag_need_data = 0x04,
};

enum {
  k_intel_fdc_result_ok = 0x00,
  k_intel_fdc_result_clock_error = 0x08,
  k_intel_fdc_result_late_dma = 0x0A,
  k_intel_fdc_result_id_crc_error = 0x0C,
  k_intel_fdc_result_data_crc_error = 0x0E,
  k_intel_fdc_result_write_protected = 0x12,
  k_intel_fdc_result_sector_not_found = 0x18,
  k_intel_fdc_result_flag_deleted_data = 0x20,
};

enum {
  k_intel_fdc_command_SCAN_DATA = 0,
  k_intel_fdc_command_SCAN_DATA_AND_DELETED = 1,
  k_intel_fdc_command_WRITE_DATA = 2,
  k_intel_fdc_command_WRITE_DELETED_DATA = 3,
  k_intel_fdc_command_READ_DATA = 4,
  k_intel_fdc_command_READ_DATA_AND_DELETED = 5,
  k_intel_fdc_command_READ_ID = 6,
  k_intel_fdc_command_VERIFY = 7,
  k_intel_fdc_command_FORMAT = 8,
  k_intel_fdc_command_UNUSED_9 = 9,
  k_intel_fdc_command_SEEK = 10,
  k_intel_fdc_command_READ_DRIVE_STATUS = 11,
  k_intel_fdc_command_UNUSED_12 = 12,
  k_intel_fdc_command_SPECIFY = 13,
  k_intel_fdc_command_WRITE_SPECIAL_REGISTER = 14,
  k_intel_fdc_command_READ_SPECIAL_REGISTER = 15,
};

enum {
  k_intel_fdc_register_internal_pointer = 0x00,
  k_intel_fdc_register_internal_count_msb_copy = 0x00,
  k_intel_fdc_register_internal_param_count = 0x01,
  k_intel_fdc_register_internal_seek_retry_count = 0x01,
  k_intel_fdc_register_internal_param_data_marker = 0x02,
  k_intel_fdc_register_internal_param_5 = 0x03,
  k_intel_fdc_register_internal_param_4 = 0x04,
  k_intel_fdc_register_internal_param_3 = 0x05,
  k_intel_fdc_register_current_sector = 0x06,
  k_intel_fdc_register_internal_param_2 = 0x06,
  k_intel_fdc_register_internal_param_1 = 0x07,
  k_intel_fdc_register_internal_header_pointer = 0x08,
  k_intel_fdc_register_internal_seek_count = 0x0A,
  k_intel_fdc_register_internal_id_sector = 0x0A,
  k_intel_fdc_register_internal_seek_target_1 = 0x0B,
  k_intel_fdc_register_internal_seek_target_2 = 0x0C,
  k_intel_fdc_register_internal_id_track = 0x0C,
  k_intel_fdc_register_head_step_rate = 0x0D,
  k_intel_fdc_register_head_settle_time = 0x0E,
  k_intel_fdc_register_head_load_unload = 0x0F,
  k_intel_fdc_register_bad_track_1_drive_0 = 0x10,
  k_intel_fdc_register_bad_track_2_drive_0 = 0x11,
  k_intel_fdc_register_track_drive_0 = 0x12,
  k_intel_fdc_register_internal_count_lsb = 0x13,
  k_intel_fdc_register_internal_count_msb = 0x14,
  k_intel_fdc_register_internal_result = 0x16,
  k_intel_fdc_register_mode = 0x17,
  k_intel_fdc_register_internal_status = 0x17,
  k_intel_fdc_register_bad_track_1_drive_1 = 0x18,
  k_intel_fdc_register_bad_track_2_drive_1 = 0x19,
  k_intel_fdc_register_track_drive_1 = 0x1A,
  k_intel_fdc_register_internal_index_pulse_count = 0x1C,
  k_intel_fdc_register_internal_data = 0x1D,
  k_intel_fdc_register_internal_parameter = 0x1E,
  k_intel_fdc_register_internal_command = 0x1F,
  k_intel_fdc_register_drive_out = 0x23,
};

enum {
  k_intel_fdc_drive_out_select_1 = 0x80,
  k_intel_fdc_drive_out_select_0 = 0x40,
  k_intel_fdc_drive_out_side = 0x20,
  k_intel_fdc_drive_out_low_head_current = 0x10,
  k_intel_fdc_drive_out_load_head = 0x08,
  k_intel_fdc_drive_out_direction = 0x04,
  k_intel_fdc_drive_out_step = 0x02,
  k_intel_fdc_drive_out_write_enable = 0x01,
};

enum {
  k_intel_fdc_mode_single_actuator = 0x02,
  k_intel_fdc_mode_dma = 0x01,
};

enum {
  k_intel_fdc_state_null = 0,
  k_intel_fdc_state_idle,
  k_intel_fdc_state_wait_no_index,
  k_intel_fdc_state_wait_index,
  k_intel_fdc_state_syncing_for_id_wait,
  k_intel_fdc_state_syncing_for_id,
  k_intel_fdc_state_check_id_marker,
  k_intel_fdc_state_in_id,
  k_intel_fdc_state_in_id_crc,
  k_intel_fdc_state_syncing_for_data_wait,
  k_intel_fdc_state_syncing_for_data,
  k_intel_fdc_state_check_data_marker,
  k_intel_fdc_state_in_data,
  k_intel_fdc_state_in_deleted_data,
  k_intel_fdc_state_in_data_crc,
  k_intel_fdc_state_write_gap_2,
  k_intel_fdc_state_write_sector_data,
  k_intel_fdc_state_format_gap_1,
  k_intel_fdc_state_format_write_id,
  k_intel_fdc_state_format_write_data,
  k_intel_fdc_state_format_gap_3,
  k_intel_fdc_state_format_gap_4,
  k_intel_fdc_state_seek_setup,
  k_intel_fdc_state_seeking,
  k_intel_fdc_state_settle_or_load_setup,
  k_intel_fdc_state_settle_or_load,
};

enum {
  k_intel_num_registers = 32,
};

struct intel_fdc_struct {
  struct state_6502* p_state_6502;

  int log_commands;

  struct disc_drive_struct* p_drive_0;
  struct disc_drive_struct* p_drive_1;
  struct disc_drive_struct* p_current_drive;

  /* Event callbacks. */
  int parameter_callback;
  int index_pulse_callback;

  uint8_t regs[k_intel_num_registers];
  uint8_t drive_out;

  uint32_t shift_register;
  uint32_t num_shifts;

  int current_needs_settle;
  int32_t current_head_unload_count;
  uint32_t current_seek_count;

  int state;
  uint32_t state_count;
  int state_is_index_pulse;
  uint16_t crc;
  uint16_t on_disc_crc;
};

static inline uint8_t
intel_fdc_get_status(struct intel_fdc_struct* p_fdc) {
  return p_fdc->regs[k_intel_fdc_register_internal_status];
}

static inline void
intel_fdc_set_status(struct intel_fdc_struct* p_fdc, uint8_t status) {
  p_fdc->regs[k_intel_fdc_register_internal_status] = status;
}

static inline uint8_t
intel_fdc_get_result(struct intel_fdc_struct* p_fdc) {
  return p_fdc->regs[k_intel_fdc_register_internal_result];
}

static inline uint8_t
intel_fdc_get_internal_command(struct intel_fdc_struct* p_fdc) {
  uint8_t command = p_fdc->regs[k_intel_fdc_register_internal_command];
  command &= 0x3C;
  return (command >> 2);
}

static int
intel_fdc_command_is_writing(struct intel_fdc_struct* p_fdc) {
  uint8_t command = intel_fdc_get_internal_command(p_fdc);
  if ((command == k_intel_fdc_command_WRITE_DATA) ||
      (command == k_intel_fdc_command_WRITE_DELETED_DATA) ||
      (command == k_intel_fdc_command_FORMAT)) {
    return 1;
  }
  return 0;
}

static inline uint8_t
intel_fdc_get_select_bits(struct intel_fdc_struct* p_fdc) {
  return (p_fdc->drive_out & 0xC0);
}

static inline uint32_t
intel_fdc_get_sector_size(struct intel_fdc_struct* p_fdc) {
  uint32_t size = (p_fdc->regs[k_intel_fdc_register_internal_param_3] >> 5);
  size = (128 << size);
  return size;
}

static void
intel_fdc_setup_sector_size(struct intel_fdc_struct* p_fdc) {
  /* TODO: actually use this as the decrementing count. */
  uint32_t size = intel_fdc_get_sector_size(p_fdc);
  uint32_t msb = ((size / 128) - 1);
  p_fdc->regs[k_intel_fdc_register_internal_count_lsb] = 0x80;
  p_fdc->regs[k_intel_fdc_register_internal_count_msb] = msb;
  /* NOTE: this is R0, i.e. R0 is trashed here. */
  p_fdc->regs[k_intel_fdc_register_internal_count_msb_copy] = msb;
}

static void
intel_fdc_start_index_pulse_timeout(struct intel_fdc_struct* p_fdc) {
  p_fdc->regs[k_intel_fdc_register_internal_index_pulse_count] = 3;
  p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_timeout;
}

static void
intel_fdc_set_drive_out(struct intel_fdc_struct* p_fdc, uint8_t drive_out) {
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;

  if (p_current_drive != NULL) {
    if ((p_fdc->drive_out & k_intel_fdc_drive_out_load_head) !=
        (drive_out & k_intel_fdc_drive_out_load_head)) {
      if (drive_out & k_intel_fdc_drive_out_load_head) {
        disc_drive_start_spinning(p_current_drive);
      } else {
        disc_drive_stop_spinning(p_current_drive);
      }
    }
    disc_drive_select_side(p_current_drive,
                           !!(drive_out & k_intel_fdc_drive_out_side));
  }

  p_fdc->drive_out = drive_out;
}

static void
intel_fdc_drive_out_raise(struct intel_fdc_struct* p_fdc, uint8_t bits) {
  uint8_t drive_out = (p_fdc->drive_out | bits);
  intel_fdc_set_drive_out(p_fdc, drive_out);
}

static void
intel_fdc_drive_out_lower(struct intel_fdc_struct* p_fdc, uint8_t bits) {
  uint8_t drive_out = (p_fdc->drive_out & ~bits);
  intel_fdc_set_drive_out(p_fdc, drive_out);
}

static void
intel_fdc_select_drive(struct intel_fdc_struct* p_fdc,
                       uint8_t new_drive_select) {
  uint8_t new_drive_out;

  if (new_drive_select == intel_fdc_get_select_bits(p_fdc)) {
    return;
  }

  /* TODO: doesn't belong here. */
  /* A change of drive select bits clears all drive out bits other than side
   * select.
   * For example, the newly selected drive won't have the load head signal
   * active. This spins down any previously selected drive.
   */
  new_drive_out = new_drive_select;
  new_drive_out |= (p_fdc->drive_out & k_intel_fdc_drive_out_side);
  intel_fdc_set_drive_out(p_fdc, new_drive_out);

  if (new_drive_select == 0x40) {
    p_fdc->p_current_drive = p_fdc->p_drive_0;
  } else if (new_drive_select == 0x80) {
    p_fdc->p_current_drive = p_fdc->p_drive_1;
  } else {
    /* NOTE: I'm not sure what selecting no drive or selecting both drives is
     * supposed to do.
     */
    p_fdc->p_current_drive = NULL;
  }
}

static void
intel_fdc_set_state(struct intel_fdc_struct* p_fdc, int state) {
  uint8_t head_unload_count;

  p_fdc->state = state;
  p_fdc->state_count = 0;

  if ((state == k_intel_fdc_state_syncing_for_id) ||
      (state == k_intel_fdc_state_syncing_for_data)) {
    p_fdc->shift_register = 0;
    p_fdc->num_shifts = 0;
  }
  if (state != k_intel_fdc_state_idle) {
    return;
  }

  p_fdc->drive_out &= ~k_intel_fdc_drive_out_write_enable;
  head_unload_count = (p_fdc->regs[k_intel_fdc_register_head_load_unload] >> 4);

  p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_none;
  if (head_unload_count == 0) {
    /* Unload immediately. */
    intel_fdc_select_drive(p_fdc, 0);
  } else if (head_unload_count == 0xF) {
    /* Never automatically unload. */
  } else {
    p_fdc->regs[k_intel_fdc_register_internal_index_pulse_count] =
        head_unload_count;
    p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_spindown;
  }
}

static void
intel_fdc_command_abort(struct intel_fdc_struct* p_fdc) {
  /* If we're aborting a command in the middle of writing data, it usually
   * doesn't leave a clean byte end on the disc. This is not particularly
   * important to emulate at all but it does help create new copy protection
   * schemes under emulation.
   */
  if ((p_fdc->state == k_intel_fdc_state_write_sector_data) ||
      (p_fdc->state == k_intel_fdc_state_format_write_id) ||
      (p_fdc->state == k_intel_fdc_state_format_write_data)) {
    disc_drive_write_byte(p_fdc->p_current_drive, 0xFF, 0xFF);
  }

  /* Lower any NMI assertion. This is particularly important for error $0A,
   * aka. late DMA, which will abort the command while NMI is asserted. We
   * therefore need to de-assert NMI so that the NMI for command completion
   * isn't lost.
   */
  state_6502_set_irq_level(p_fdc->p_state_6502, k_state_6502_irq_nmi, 0);
}

void
intel_fdc_power_on_reset(struct intel_fdc_struct* p_fdc) {
  /* The chip's reset line does take care of a lot of things.... */
  intel_fdc_break_reset(p_fdc);
  /* ... but not everything. Note that not all of these have been verified as
   * to whether the reset line changes them or not.
   */
  assert(p_fdc->parameter_callback == k_intel_fdc_parameter_accept_none);
  assert(p_fdc->state == k_intel_fdc_state_idle);
  assert(intel_fdc_get_select_bits(p_fdc) == 0);
  assert(p_fdc->p_current_drive == NULL);

  (void) memset(&p_fdc->regs[0], '\0', sizeof(p_fdc->regs));
  p_fdc->drive_out = 0;
  p_fdc->state_count = 0;
  p_fdc->state_is_index_pulse = 0;
}

void
intel_fdc_break_reset(struct intel_fdc_struct* p_fdc) {
  /* Abort any in-progress command. */
  intel_fdc_command_abort(p_fdc);
  intel_fdc_set_state(p_fdc, k_intel_fdc_state_idle);
  p_fdc->parameter_callback = k_intel_fdc_parameter_accept_none;
  p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_none;

  /* Deselect any drive; ensures spin-down. */
  intel_fdc_select_drive(p_fdc, 0);

  /* EMU: on a real machine, status appears to be cleared but result and data
   * register not. */
  intel_fdc_set_status(p_fdc, 0);
}

struct intel_fdc_struct*
intel_fdc_create(struct state_6502* p_state_6502,
                 struct bbc_options* p_options) {
  struct intel_fdc_struct* p_fdc =
      util_mallocz(sizeof(struct intel_fdc_struct));

  p_fdc->p_state_6502 = p_state_6502;

  p_fdc->log_commands = util_has_option(p_options->p_log_flags,
                                        "disc:commands");

  intel_fdc_set_state(p_fdc, k_intel_fdc_state_idle);

  return p_fdc;
}

void
intel_fdc_destroy(struct intel_fdc_struct* p_fdc) {
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
intel_fdc_set_status_result(struct intel_fdc_struct* p_fdc,
                            uint8_t status,
                            uint8_t result) {
  struct state_6502* p_state_6502 = p_fdc->p_state_6502;
  int level = !!(status & k_intel_fdc_status_flag_nmi);
  int firing = state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi);

  intel_fdc_set_status(p_fdc, status);
  p_fdc->regs[k_intel_fdc_register_internal_result] = result;

  if (firing && (level == 1)) {
    log_do_log(k_log_disc, k_log_error, "edge triggered NMI already high");
  }

  state_6502_set_irq_level(p_state_6502, k_state_6502_irq_nmi, level);
}

static void
intel_fdc_set_command_result(struct intel_fdc_struct* p_fdc,
                             int do_nmi,
                             uint8_t result) {
  /* TODO: update status, don't replace it. */
  uint8_t status = k_intel_fdc_status_flag_result_ready;
  if (do_nmi) {
    status |= k_intel_fdc_status_flag_nmi;
  }

  if (p_fdc->log_commands) {
    log_do_log(k_log_disc,
               k_log_info,
               "8271: status $%x result $%x",
               status,
               result);
  }

  intel_fdc_set_status_result(p_fdc, status, result);

  intel_fdc_set_state(p_fdc, k_intel_fdc_state_idle);
}

static void
intel_fdc_merge_command_result(struct intel_fdc_struct* p_fdc, uint8_t result) {
  if (result != k_intel_fdc_result_ok) {
    intel_fdc_drive_out_lower(p_fdc, (k_intel_fdc_drive_out_direction |
                                          k_intel_fdc_drive_out_step |
                                          k_intel_fdc_drive_out_write_enable));
  }
  result |= intel_fdc_get_result(p_fdc);
  p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_none;
  intel_fdc_set_command_result(p_fdc, 1, result);
}

uint8_t
intel_fdc_read(struct intel_fdc_struct* p_fdc, uint16_t addr) {
  uint8_t result;

  switch (addr & 0x07) {
  case k_intel_fdc_status:
    return intel_fdc_get_status(p_fdc);
  case k_intel_fdc_result:
    result = intel_fdc_get_result(p_fdc);
    intel_fdc_set_status_result(p_fdc,
                                (intel_fdc_get_status(p_fdc) &
                                     ~(k_intel_fdc_status_flag_result_ready |
                                           k_intel_fdc_status_flag_nmi)),
                                result);
    return result;
  /* EMU: on a real model B, the i8271 has the data register mapped for all of
   * register address 4 - 7.
   */
  case k_intel_fdc_data:
  case (k_intel_fdc_data + 1):
  case (k_intel_fdc_data + 2):
  case (k_intel_fdc_data + 3):
    result = intel_fdc_get_result(p_fdc);
    intel_fdc_set_status_result(p_fdc,
                                (intel_fdc_get_status(p_fdc) &
                                     ~(k_intel_fdc_status_flag_need_data |
                                           k_intel_fdc_status_flag_nmi)),
                                result);
    return p_fdc->regs[k_intel_fdc_register_internal_data];
  case k_intel_fdc_unknown_read_2:
  case k_intel_fdc_unknown_read_3:
    /* EMU: register address 2 and 3 are not documented as having anything
     * wired up for reading, BUT on a model B, I'm seeing:
     * R2 == 255, R3 == 184 after machine power on.
     * Both 0 after some successful disc operation.
     */
    return 0;
  default:
    assert(0);
    return 0;
  }
}

static int
intel_fdc_get_WRPROT(struct intel_fdc_struct* p_fdc) {
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  if (p_current_drive == NULL) {
    return 0;
  }
  return disc_drive_is_write_protect(p_current_drive);
}

static int
intel_fdc_get_TRK0(struct intel_fdc_struct* p_fdc) {
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  if (p_current_drive == NULL) {
    return 0;
  }
  return (disc_drive_get_track(p_current_drive) == 0);
}

static int
intel_fdc_get_INDEX(struct intel_fdc_struct* p_fdc) {
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  if (p_current_drive == NULL) {
    return 0;
  }
  return disc_drive_is_index_pulse(p_current_drive);
}

static int
intel_fdc_current_disc_is_spinning(struct intel_fdc_struct* p_fdc) {
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  if (p_current_drive == NULL) {
    return 0;
  }
  return disc_drive_is_spinning(p_current_drive);
}

static uint8_t
intel_fdc_read_special_register(struct intel_fdc_struct* p_fdc, uint8_t reg) {
  uint8_t ret = 0;

  reg = (reg & 0x3F);
  if (reg < k_intel_num_registers) {
    return p_fdc->regs[reg];
  }
  reg = (reg & 0x07);
  switch (reg) {
  case k_intel_fdc_register_drive_out:
    /* DFS-1.2 reads drive out in normal operation. */
    ret = p_fdc->drive_out;
    break;
  default:
    log_do_log(k_log_disc,
               k_log_unimplemented,
               "direct read to MMIO register %d",
               reg);
    break;
  }

  return ret;
}

static void
intel_fdc_write_register(struct intel_fdc_struct* p_fdc,
                         uint8_t reg,
                         uint8_t val) {
  reg = (reg & 0x3F);
  if (reg < k_intel_num_registers) {
    p_fdc->regs[reg] = val;
    return;
  }
  reg = (reg & 0x07);
  switch (reg) {
  case (k_intel_fdc_register_drive_out & 0x07):
    /* Bit 0x20 is important as it's used to select the side of the disc for
     * double sided discs.
     * Bit 0x08 is important as it provides manual head load / unload control,
     * which includes motor spin up / down.
     * The parameter also includes drive select bits which override those in
     * the command.
     */
    /* Select the drive as a separate call to avoid recursion between
     * these two functions.
     */
    intel_fdc_select_drive(p_fdc, (val & 0xC0));
    intel_fdc_set_drive_out(p_fdc, val);
    break;
  default:
    log_do_log(k_log_disc,
               k_log_unimplemented,
               "direct write to MMIO register %d",
               reg);
    break;
  }
}

static void
intel_fdc_start_syncing_for_header(struct intel_fdc_struct* p_fdc) {
  p_fdc->regs[k_intel_fdc_register_internal_header_pointer] = 0x0C;
  intel_fdc_set_state(p_fdc, k_intel_fdc_state_syncing_for_id);
}

static void
intel_fdc_do_command_dispatch(struct intel_fdc_struct* p_fdc) {
  uint8_t temp_u8;
  uint8_t command = intel_fdc_get_internal_command(p_fdc);

  switch (command) {
  case k_intel_fdc_command_UNUSED_9:
  case k_intel_fdc_command_UNUSED_12:
    util_bail("unused 8271 command");
    break;
  case k_intel_fdc_command_READ_DRIVE_STATUS:
    temp_u8 = 0x80;
    if (!intel_fdc_current_disc_is_spinning(p_fdc)) {
      intel_fdc_set_command_result(p_fdc, 0, temp_u8);
      break;
    }
    if (intel_fdc_get_TRK0(p_fdc)) {
      /* TRK0 */
      temp_u8 |= 0x02;
    }
    if (intel_fdc_get_select_bits(p_fdc) & 0x40) {
      /* RDY0 */
      temp_u8 |= 0x04;
    }
    if (intel_fdc_get_select_bits(p_fdc) & 0x80) {
      /* RDY1 */
      temp_u8 |= 0x40;
    }
    if (intel_fdc_get_WRPROT(p_fdc)) {
      /* WR PROT */
      temp_u8 |= 0x08;
    }
    if (intel_fdc_get_INDEX(p_fdc)) {
      /* INDEX */
      temp_u8 |= 0x10;
    }
    intel_fdc_set_command_result(p_fdc, 0, temp_u8);
    break;
  case k_intel_fdc_command_SPECIFY:
    p_fdc->regs[k_intel_fdc_register_internal_pointer] =
        p_fdc->regs[k_intel_fdc_register_internal_param_1];
    p_fdc->regs[k_intel_fdc_register_internal_param_count] = 3;
    p_fdc->parameter_callback = k_intel_fdc_parameter_accept_specify;
    break;
  case k_intel_fdc_command_WRITE_SPECIAL_REGISTER:
    intel_fdc_write_register(
        p_fdc,
        p_fdc->regs[k_intel_fdc_register_internal_param_1],
        p_fdc->regs[k_intel_fdc_register_internal_param_2]);
    /* EMU: checked on a real 8271.  */
    intel_fdc_set_command_result(p_fdc, 0, k_intel_fdc_result_ok);
    break;
  case k_intel_fdc_command_READ_SPECIAL_REGISTER:
    temp_u8 = intel_fdc_read_special_register(
        p_fdc, p_fdc->regs[k_intel_fdc_register_internal_param_1]);
    intel_fdc_set_command_result(p_fdc, 0, temp_u8);
    break;
  case k_intel_fdc_command_READ_ID:
    /* First dispatch for the command, we go through the seek / wait for index /
     * etc. rigamarole. The command is re-dispatched for the second and further
     * headers, where we just straight to searching for header sync.
     * This can also be used as an undocumented mode of READ_ID where a
     * non-zero value to the second parameter will skip syncing to the index
     * pulse.
     */
    if (p_fdc->regs[k_intel_fdc_register_internal_param_2] == 0) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_seek_setup);
    } else {
      intel_fdc_start_syncing_for_header(p_fdc);
    }
    break;
  default:
    if (command == k_intel_fdc_command_WRITE_DATA) {
      p_fdc->regs[k_intel_fdc_register_internal_param_data_marker] =
          k_ibm_disc_data_mark_data_pattern;
    } else if (command == k_intel_fdc_command_WRITE_DELETED_DATA) {
      p_fdc->regs[k_intel_fdc_register_internal_param_data_marker] =
          k_ibm_disc_deleted_data_mark_data_pattern;
    }
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_seek_setup);
    break;
  }
}

static void
intel_fdc_start_command(struct intel_fdc_struct* p_fdc) {
  uint8_t command;

  uint8_t command_reg = p_fdc->regs[k_intel_fdc_register_internal_command];
  uint8_t orig_command = command_reg;

  /* TODO: the 8271 reads drive status here and changes R21 + R27. */

  p_fdc->parameter_callback = k_intel_fdc_parameter_accept_none;

  /* Select the drive before logging so that head position is reported. */
  intel_fdc_select_drive(p_fdc, (command_reg & 0xC0));
  /* Mask out drive select bits from command register, and parameter count. */
  command_reg &= 0x3C;
  p_fdc->regs[k_intel_fdc_register_internal_command] = command_reg;

  if (p_fdc->log_commands) {
    int32_t head_pos = -1;
    int32_t track = -1;
    struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;

    if (p_current_drive != NULL) {
      head_pos = (int32_t) disc_drive_get_head_position(p_current_drive);
      track = (int32_t) disc_drive_get_track(p_current_drive);
    }
    log_do_log(k_log_disc,
               k_log_info,
               "8271: command $%x sel $%x params $%x $%x $%x $%x $%x "
               "ptrk %d hpos %d",
               (orig_command & 0x3F),
               intel_fdc_get_select_bits(p_fdc),
               p_fdc->regs[k_intel_fdc_register_internal_param_1],
               p_fdc->regs[k_intel_fdc_register_internal_param_2],
               p_fdc->regs[k_intel_fdc_register_internal_param_3],
               p_fdc->regs[k_intel_fdc_register_internal_param_4],
               p_fdc->regs[k_intel_fdc_register_internal_param_5],
               track,
               head_pos);
  }

  command = intel_fdc_get_internal_command(p_fdc);

  if ((command == k_intel_fdc_command_SCAN_DATA) ||
      (command == k_intel_fdc_command_SCAN_DATA_AND_DELETED)) {
    log_do_log(k_log_disc,
               k_log_unusual,
               "8271: scan sectors doesn't work in a beeb");
  }

  p_fdc->current_needs_settle = 0;
  p_fdc->current_seek_count = 0;

  intel_fdc_do_command_dispatch(p_fdc);
}

static void
intel_fdc_command_written(struct intel_fdc_struct* p_fdc, uint8_t val) {
  uint8_t num_params;

  uint8_t status = intel_fdc_get_status(p_fdc);

  if (status & k_intel_fdc_status_flag_busy) {
    log_do_log(k_log_disc,
               k_log_unusual,
               "8271: command $%.2X while busy with $%.2X",
               val,
               p_fdc->regs[k_intel_fdc_register_internal_command]);
  }

  /* Set command. */
  p_fdc->regs[k_intel_fdc_register_internal_command] = val;
  /* Set busy, lower command full in status. */
  status |= k_intel_fdc_status_flag_busy;
  status &= ~k_intel_fdc_status_flag_command_full;
  intel_fdc_set_status(p_fdc, status);
  /* Set result to zero. */
  p_fdc->regs[k_intel_fdc_register_internal_result] = 0;

  /* Default parameters. This supports the 1x128 byte sector commands. */
  p_fdc->regs[k_intel_fdc_register_internal_param_3] = 1;
  p_fdc->regs[k_intel_fdc_register_internal_param_4] = 1;

  /* Calculate parameters expected. This is the exact logic in the 8271 ROM. */
  num_params = 5;
  if (p_fdc->regs[k_intel_fdc_register_internal_command] & 0x18) {
    num_params = (p_fdc->regs[k_intel_fdc_register_internal_command] & 0x03);
  }

  /* Expectation goes in R1. */
  p_fdc->regs[k_intel_fdc_register_internal_param_count] = num_params;

  /* Exit to wait for parameters if necessary. */
  if (num_params > 0) {
    /* Parameters write into R7 downwards. */
    p_fdc->regs[k_intel_fdc_register_internal_pointer] = 7;
    p_fdc->parameter_callback = k_intel_fdc_parameter_accept_command;
    return;
  }

  intel_fdc_start_command(p_fdc);
}

static void
intel_fdc_param_written(struct intel_fdc_struct* p_fdc, uint8_t val) {
  p_fdc->regs[k_intel_fdc_register_internal_parameter] = val;

  switch (p_fdc->parameter_callback) {
  case k_intel_fdc_parameter_accept_none:
    break;
  case k_intel_fdc_parameter_accept_command:
    intel_fdc_write_register(
        p_fdc,
        p_fdc->regs[k_intel_fdc_register_internal_pointer],
        p_fdc->regs[k_intel_fdc_register_internal_parameter]);
    p_fdc->regs[k_intel_fdc_register_internal_pointer]--;
    p_fdc->regs[k_intel_fdc_register_internal_param_count]--;
    if (p_fdc->regs[k_intel_fdc_register_internal_param_count] == 0) {
      intel_fdc_start_command(p_fdc);
    }
    break;
  case k_intel_fdc_parameter_accept_specify:
    intel_fdc_write_register(
        p_fdc,
        p_fdc->regs[k_intel_fdc_register_internal_pointer],
        p_fdc->regs[k_intel_fdc_register_internal_parameter]);
    p_fdc->regs[k_intel_fdc_register_internal_pointer]++;
    p_fdc->regs[k_intel_fdc_register_internal_param_count]--;
    if (p_fdc->regs[k_intel_fdc_register_internal_param_count] == 0) {
      /* EMU: return value matches real 8271. */
      intel_fdc_set_command_result(p_fdc, 0, k_intel_fdc_result_ok);
    }
    break;
  default:
    assert(0);
    break;
  }
}

void
intel_fdc_write(struct intel_fdc_struct* p_fdc,
                uint16_t addr,
                uint8_t val) {
  uint8_t result;

  switch (addr & 0x07) {
  case k_intel_fdc_command:
    intel_fdc_command_written(p_fdc, val);
    break;
  case k_intel_fdc_parameter:
    intel_fdc_param_written(p_fdc, val);
    break;
  case k_intel_fdc_data:
  case (k_intel_fdc_data + 1):
  case (k_intel_fdc_data + 2):
  case (k_intel_fdc_data + 3):
    result = intel_fdc_get_result(p_fdc);
    intel_fdc_set_status_result(p_fdc,
                                (intel_fdc_get_status(p_fdc) &
                                     ~(k_intel_fdc_status_flag_need_data |
                                           k_intel_fdc_status_flag_nmi)),
                                result);
    p_fdc->regs[k_intel_fdc_register_internal_data] = val;
    break;
  case k_intel_fdc_reset:
    /* EMU: On a real 8271, crazy crazy things happen if you write 2 or
     * especially 4 to this register.
     */
    assert((val == 0) || (val == 1));

    /* EMU TODO: if we cared to emulate this more accurately, note that it's
     * possible to leave the reset register in the 1 state and then commands
     * to the command register are ignored.
     */
    if (val == 1) {
      if (p_fdc->log_commands) {
        log_do_log(k_log_disc, k_log_info, "8271: reset");
      }
      intel_fdc_break_reset(p_fdc);
    }
    break;
  case 3:
    log_do_log(k_log_disc, k_log_info, "8271: write to unmapped register 3");
    break;
  default:
    assert(0);
    break;
  }
}

static int
intel_fdc_check_data_loss_ok(struct intel_fdc_struct* p_fdc) {
  int ok = 1;
  uint8_t command = intel_fdc_get_internal_command(p_fdc);

  /* Abort command if it's any type of scan. The 8271 requires DMA to be wired
   * up for scan commands, which is not done in the BBC application.
   */
  if ((command == k_intel_fdc_command_SCAN_DATA) ||
      (command == k_intel_fdc_command_SCAN_DATA_AND_DELETED)) {
    ok = 0;
  }

  /* Abort command if previous data byte wasn't picked up. */
  if (intel_fdc_get_status(p_fdc) & k_intel_fdc_status_flag_need_data) {
    ok = 0;
  }

  if (ok) {
    return 1;
  }

  intel_fdc_command_abort(p_fdc);
  intel_fdc_merge_command_result(p_fdc, k_intel_fdc_result_late_dma);

  return 0;
}

static int
intel_fdc_provide_data_byte(struct intel_fdc_struct* p_fdc, uint8_t byte) {
  if (!intel_fdc_check_data_loss_ok(p_fdc)) {
    return 0;
  }
  p_fdc->regs[k_intel_fdc_register_internal_data] = byte;
  intel_fdc_set_status_result(p_fdc,
                              (intel_fdc_get_status(p_fdc) |
                                  k_intel_fdc_status_flag_nmi |
                                  k_intel_fdc_status_flag_need_data),
                              k_intel_fdc_result_ok);
  return 1;
}

static int
intel_fdc_consume_data_byte(struct intel_fdc_struct* p_fdc) {
  uint8_t data = p_fdc->regs[k_intel_fdc_register_internal_data];

  if (!intel_fdc_check_data_loss_ok(p_fdc)) {
    return 0;
  }

  disc_drive_write_byte(p_fdc->p_current_drive, data, 0xFF);
  return 1;
}

static int
intel_fdc_check_crc(struct intel_fdc_struct* p_fdc, uint8_t error) {
  if (p_fdc->crc == p_fdc->on_disc_crc) {
    return 1;
  }

  intel_fdc_merge_command_result(p_fdc, error);
  return 0;
}

static void
intel_fdc_check_completion(struct intel_fdc_struct* p_fdc) {
  /* Specifying 0 sectors seems to result in 32 read, due to underflow of the
   * 5-bit counter.
   * On commands other than READ_ID, any underflow has other side effects
   * such as modifying the sector size.
   */
  /* TODO: 8271 does a check drive ready here. */
  /* Lower WRITE_ENABLE. */
  p_fdc->drive_out &= ~k_intel_fdc_drive_out_write_enable;
  /* One less sector to go. */
  p_fdc->regs[k_intel_fdc_register_internal_param_3]--;
  if ((p_fdc->regs[k_intel_fdc_register_internal_param_3] & 0x1F) == 0) {
    intel_fdc_merge_command_result(p_fdc, k_intel_fdc_result_ok);
  } else {
    /* This looks strange as it is set up to be just an increment (R4==1 in
     * sector operations), but it is what the 8271 ROM does.
     */
    p_fdc->regs[k_intel_fdc_register_internal_param_2] +=
        (p_fdc->regs[k_intel_fdc_register_internal_param_4] & 0x3F);
    /* This is also what the 8271 ROM does, just re-dispatches the current
     * command.
     */
    intel_fdc_do_command_dispatch(p_fdc);
  }
}

static void
intel_fdc_byte_callback_reading(struct intel_fdc_struct* p_fdc,
                                uint8_t data_byte,
                                uint8_t clocks_byte) {
  /* NOTE: this callback routine is also used for seek / settle timing,
   * which is not a precise 64us basis.
   */
  uint8_t command = intel_fdc_get_internal_command(p_fdc);

  switch (p_fdc->state) {
  case k_intel_fdc_state_check_id_marker:
    if ((clocks_byte == k_ibm_disc_mark_clock_pattern) &&
        (data_byte == k_ibm_disc_id_mark_data_pattern)) {
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc =
          ibm_disc_format_crc_add_byte(p_fdc->crc,
                                       k_ibm_disc_id_mark_data_pattern);

      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_id);
    } else {
      intel_fdc_start_syncing_for_header(p_fdc);
    }
    break;
  case k_intel_fdc_state_in_id:
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
    if (command == k_intel_fdc_command_READ_ID) {
      if (!intel_fdc_provide_data_byte(p_fdc, data_byte)) {
        break;
      }
    }
    intel_fdc_write_register(
        p_fdc,
        p_fdc->regs[k_intel_fdc_register_internal_header_pointer],
        data_byte);
    p_fdc->regs[k_intel_fdc_register_internal_header_pointer]--;
    if ((p_fdc->regs[k_intel_fdc_register_internal_header_pointer] & 0x07) ==
            0) {
      p_fdc->on_disc_crc = 0;
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_id_crc);
    }
    break;
  case k_intel_fdc_state_in_id_crc:
    p_fdc->on_disc_crc <<= 8;
    p_fdc->on_disc_crc |= data_byte;
    p_fdc->state_count++;
    if (p_fdc->state_count == 2) {
      /* EMU NOTE: on a real 8271, an ID CRC error seems to end things
       * decisively even if a subsequent ok ID would match.
       */
      if (!intel_fdc_check_crc(p_fdc, k_intel_fdc_result_id_crc_error)) {
        break;
      }
      if (command == k_intel_fdc_command_READ_ID) {
        intel_fdc_check_completion(p_fdc);
      } else if (p_fdc->regs[k_intel_fdc_register_internal_id_track] !=
                 p_fdc->regs[k_intel_fdc_register_internal_param_1]) {
        /* EMU NOTE: upon any mismatch of found track vs. expected track,
         * the drive will try twice more on the next two tracks.
         */
        p_fdc->regs[k_intel_fdc_register_internal_seek_retry_count]++;
        if (p_fdc->regs[k_intel_fdc_register_internal_seek_retry_count] == 3) {
          intel_fdc_merge_command_result(p_fdc,
                                         k_intel_fdc_result_sector_not_found);
        } else {
          intel_fdc_set_state(p_fdc, k_intel_fdc_state_seek_setup);
        }
      } else if (p_fdc->regs[k_intel_fdc_register_internal_id_sector] ==
                 p_fdc->regs[k_intel_fdc_register_internal_param_2]) {
        if (intel_fdc_command_is_writing(p_fdc)) {
          intel_fdc_set_state(p_fdc, k_intel_fdc_state_write_gap_2);
        } else {
          intel_fdc_set_state(p_fdc, k_intel_fdc_state_syncing_for_data_wait);
        }
      } else {
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_syncing_for_id_wait);
      }
    }
    break;
  case k_intel_fdc_state_check_data_marker:
    if ((clocks_byte == k_ibm_disc_mark_clock_pattern) &&
        ((data_byte == k_ibm_disc_data_mark_data_pattern) ||
            (data_byte == k_ibm_disc_deleted_data_mark_data_pattern))) {
      int new_state = k_intel_fdc_state_in_data;
      if (data_byte == k_ibm_disc_deleted_data_mark_data_pattern) {
        p_fdc->regs[k_intel_fdc_register_internal_status] =
            k_intel_fdc_result_flag_deleted_data;
        new_state = k_intel_fdc_state_in_deleted_data;
      }
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);

      intel_fdc_set_state(p_fdc, new_state);
    } else {
      intel_fdc_merge_command_result(p_fdc, k_intel_fdc_result_clock_error);
    }
    break;
  case k_intel_fdc_state_in_data:
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
    if (command != k_intel_fdc_command_VERIFY) {
      if (!intel_fdc_provide_data_byte(p_fdc, data_byte)) {
        break;
      }
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == intel_fdc_get_sector_size(p_fdc)) {
      p_fdc->on_disc_crc = 0;
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_data_crc);
    }
    break;
  case k_intel_fdc_state_in_deleted_data:
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
    if (command == k_intel_fdc_command_READ_DATA_AND_DELETED) {
      if (!intel_fdc_provide_data_byte(p_fdc, data_byte)) {
        break;
      }
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == intel_fdc_get_sector_size(p_fdc)) {
      p_fdc->on_disc_crc = 0;
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_data_crc);
    }
    break;
  case k_intel_fdc_state_in_data_crc:
    p_fdc->on_disc_crc <<= 8;
    p_fdc->on_disc_crc |= data_byte;
    p_fdc->state_count++;
    if (p_fdc->state_count == 2) {
      if (!intel_fdc_check_crc(p_fdc, k_intel_fdc_result_data_crc_error)) {
        break;
      }
      intel_fdc_check_completion(p_fdc);
    }
    break;
  default:
    assert(0);
    break;
  }
}

static void
intel_fdc_byte_callback_writing(struct intel_fdc_struct* p_fdc) {
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;

  switch (p_fdc->state) {
  case k_intel_fdc_state_write_gap_2:
    /* EMU NOTE: we don't know when the write head turns on. Put another way,
     * where is the write splice created?
     * Here, we re-write the entirety of GAP2 starting immediately after the
     * checksum. It's possible a real chip has a delay, or only writes the 0's,
     * or something more funky entirely.
     */
    if (p_fdc->state_count < 11) {
      disc_drive_write_byte(p_current_drive, 0xFF, 0xFF);
    } else {
      disc_drive_write_byte(p_current_drive, 0x00, 0xFF);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == (11 + 6)) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_write_sector_data);
    }
    break;
  case k_intel_fdc_state_write_sector_data:
    if (p_fdc->state_count == 0) {
      uint8_t mark_byte =
          p_fdc->regs[k_intel_fdc_register_internal_param_data_marker];
      disc_drive_write_byte(p_current_drive,
                            mark_byte,
                            k_ibm_disc_mark_clock_pattern);
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, mark_byte);
    } else if (p_fdc->state_count < (intel_fdc_get_sector_size(p_fdc) + 1)) {
      uint8_t data = p_fdc->regs[k_intel_fdc_register_internal_data];
      if (!intel_fdc_consume_data_byte(p_fdc)) {
        break;
      }
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
    } else if (p_fdc->state_count == (intel_fdc_get_sector_size(p_fdc) + 1)) {
      disc_drive_write_byte(p_current_drive, (p_fdc->crc >> 8), 0xFF);
    } else if (p_fdc->state_count == (intel_fdc_get_sector_size(p_fdc) + 2)) {
      disc_drive_write_byte(p_current_drive, (p_fdc->crc & 0xFF), 0xFF);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == (intel_fdc_get_sector_size(p_fdc) + 3)) {
      intel_fdc_check_completion(p_fdc);
    } else if (p_fdc->state_count < (intel_fdc_get_sector_size(p_fdc) + 1)) {
      intel_fdc_set_status_result(p_fdc,
                                  (intel_fdc_get_status(p_fdc) |
                                      k_intel_fdc_status_flag_nmi |
                                      k_intel_fdc_status_flag_need_data),
                                  k_intel_fdc_result_ok);
    }
    break;
  case k_intel_fdc_state_format_gap_1:
    if (p_fdc->state_count <
            p_fdc->regs[k_intel_fdc_register_internal_param_5]) {
      disc_drive_write_byte(p_current_drive, 0xFF, 0xFF);
    } else {
      disc_drive_write_byte(p_current_drive, 0x00, 0xFF);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count ==
            (uint32_t) (p_fdc->regs[k_intel_fdc_register_internal_param_5] +
                            6)) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_write_id);
    }
    break;
  case k_intel_fdc_state_format_write_id:
    if (p_fdc->state_count == 0) {
      disc_drive_write_byte(p_current_drive,
                            k_ibm_disc_id_mark_data_pattern,
                            k_ibm_disc_mark_clock_pattern);
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc =
          ibm_disc_format_crc_add_byte(p_fdc->crc,
                                       k_ibm_disc_id_mark_data_pattern);
    } else if (p_fdc->state_count < 5) {
      uint8_t data = p_fdc->regs[k_intel_fdc_register_internal_data];
      if (!intel_fdc_consume_data_byte(p_fdc)) {
        break;
      }
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
    } else if (p_fdc->state_count == 5) {
      disc_drive_write_byte(p_current_drive, (p_fdc->crc >> 8), 0xFF);
    } else if (p_fdc->state_count == 6) {
      disc_drive_write_byte(p_current_drive, (p_fdc->crc & 0xFF), 0xFF);
    } else if (p_fdc->state_count < 18) {
      /* GAP 2 $FF's x11 */
      disc_drive_write_byte(p_current_drive, 0xFF, 0xFF);
    } else {
      /* GAP 2 $00's x6 */
      disc_drive_write_byte(p_current_drive, 0x00, 0xFF);
    }

    p_fdc->state_count++;
    if (p_fdc->state_count < 5) {
      intel_fdc_set_status_result(p_fdc,
                                  (intel_fdc_get_status(p_fdc) |
                                      k_intel_fdc_status_flag_nmi |
                                      k_intel_fdc_status_flag_need_data),
                                  k_intel_fdc_result_ok);
    } else if (p_fdc->state_count == (7 + 11 + 6)) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_write_data);
    }
    break;
  case k_intel_fdc_state_format_write_data:
    if (p_fdc->state_count == 0) {
      uint8_t byte = k_ibm_disc_data_mark_data_pattern;
      disc_drive_write_byte(p_current_drive,
                            byte,
                            k_ibm_disc_mark_clock_pattern);
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, byte);
    } else if (p_fdc->state_count < (intel_fdc_get_sector_size(p_fdc) + 1)) {
      uint8_t byte = 0xE5;
      disc_drive_write_byte(p_current_drive, byte, 0xFF);
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, byte);
    } else if (p_fdc->state_count == (intel_fdc_get_sector_size(p_fdc) + 1)) {
      /* Formatted sector data is constant so we can check our CRC algorithm
       * here with this assert.
       */
      if (intel_fdc_get_sector_size(p_fdc) == 256) {
        assert(p_fdc->crc == 0xA40C);
      }
      disc_drive_write_byte(p_current_drive, (p_fdc->crc >> 8), 0xFF);
    } else {
      disc_drive_write_byte(p_current_drive, (p_fdc->crc & 0xFF), 0xFF);
    }

    p_fdc->state_count++;
    if (p_fdc->state_count == (intel_fdc_get_sector_size(p_fdc) + 3)) {
      p_fdc->regs[k_intel_fdc_register_internal_param_3]--;
      if ((p_fdc->regs[k_intel_fdc_register_internal_param_3] & 0x1F) == 0) {
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_gap_4);
      } else {
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_gap_3);
      }
    }
    break;
  case k_intel_fdc_state_format_gap_3:
    if (p_fdc->state_count <
            p_fdc->regs[k_intel_fdc_register_internal_param_2]) {
      disc_drive_write_byte(p_current_drive, 0xFF, 0xFF);
    } else {
      disc_drive_write_byte(p_current_drive, 0x00, 0xFF);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count ==
            (uint32_t) (p_fdc->regs[k_intel_fdc_register_internal_param_2] +
                            6)) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_write_id);
    }
    break;
  case k_intel_fdc_state_format_gap_4:
    /* GAP 4 writes until the index pulse is hit, at which point we are done. */
    if (p_fdc->state_is_index_pulse) {
      intel_fdc_merge_command_result(p_fdc, k_intel_fdc_result_ok);
    } else {
      disc_drive_write_byte(p_current_drive, 0xFF, 0xFF);
    }
    break;
  default:
    assert(0);
    break;
  }
}

static void
intel_fdc_shift_data_bit(struct intel_fdc_struct* p_fdc, int bit) {
  uint8_t clocks_byte;
  uint8_t data_byte;
  uint32_t shift_register;
  uint32_t state_count;

  switch (p_fdc->state) {
  case k_intel_fdc_state_syncing_for_id_wait:
    p_fdc->state_count++;
    /* The controller seems to need recovery time after a sector header before
     * it can sync to another one. Measuring the "read sector IDs" command, $1B,
     * it needs 4 bytes to recover prior to the 2 bytes sync.
     */
    if (p_fdc->state_count == (4 * 8 * 2)) {
      intel_fdc_start_syncing_for_header(p_fdc);
    }
    break;
  case k_intel_fdc_state_syncing_for_data_wait:
    p_fdc->state_count++;
    /* The controller enforces a minimum byte count of 12 before sync then
     * sector data. 2 bytes of sync are needed, so absolute minumum gap here is
     * 14. The controller formats to 17 (not user controllable).
     */
    if (p_fdc->state_count == (12 * 8 * 2)) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_syncing_for_data);
    }
    break;
  case k_intel_fdc_state_syncing_for_id:
  case k_intel_fdc_state_syncing_for_data:
    state_count = p_fdc->state_count;
    /* Need to see a bit pattern of 1010101010.... to gather sync. This
     * represents a string of 1 clock bits followed by 0 data bits.
     */
    if (bit == !(state_count & 1)) {
      p_fdc->state_count++;
    } else if ((p_fdc->state_count >= 32) && (state_count & 1)) {
      /* Here, we hit a 1 data bit while in sync, so it's the start of a marker
       * byte.
       */
      assert(bit == 1);
      if (p_fdc->state == k_intel_fdc_state_syncing_for_id) {
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_check_id_marker);
      } else {
        assert(p_fdc->state == k_intel_fdc_state_syncing_for_data);
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_check_data_marker);
      }
      p_fdc->shift_register = 3;
      p_fdc->num_shifts = 2;
    } else {
      /* Restart sync. */
      p_fdc->state_count = 0;
    }
    break;
  case k_intel_fdc_state_check_id_marker:
  case k_intel_fdc_state_in_id:
  case k_intel_fdc_state_in_id_crc:
  case k_intel_fdc_state_check_data_marker:
  case k_intel_fdc_state_in_data:
  case k_intel_fdc_state_in_deleted_data:
  case k_intel_fdc_state_in_data_crc:
    shift_register = p_fdc->shift_register;
    shift_register <<= 1;
    shift_register |= bit;
    p_fdc->shift_register = shift_register;
    p_fdc->num_shifts++;

    if (p_fdc->num_shifts != 16) {
      break;
    }
    clocks_byte = 0;
    data_byte = 0;
    if (shift_register & 0x8000) clocks_byte |= 0x80;
    if (shift_register & 0x2000) clocks_byte |= 0x40;
    if (shift_register & 0x0800) clocks_byte |= 0x20;
    if (shift_register & 0x0200) clocks_byte |= 0x10;
    if (shift_register & 0x0080) clocks_byte |= 0x08;
    if (shift_register & 0x0020) clocks_byte |= 0x04;
    if (shift_register & 0x0008) clocks_byte |= 0x02;
    if (shift_register & 0x0002) clocks_byte |= 0x01;
    if (shift_register & 0x4000) data_byte |= 0x80;
    if (shift_register & 0x1000) data_byte |= 0x40;
    if (shift_register & 0x0400) data_byte |= 0x20;
    if (shift_register & 0x0100) data_byte |= 0x10;
    if (shift_register & 0x0040) data_byte |= 0x08;
    if (shift_register & 0x0010) data_byte |= 0x04;
    if (shift_register & 0x0004) data_byte |= 0x02;
    if (shift_register & 0x0001) data_byte |= 0x01;

    intel_fdc_byte_callback_reading(p_fdc, data_byte, clocks_byte);

    p_fdc->shift_register = 0;
    p_fdc->num_shifts = 0;
    break;
  /* These happen for a few bits after the end of the command if the disc
   * surface data isn't byte aligned.
   */
  case k_intel_fdc_state_idle:
  case k_intel_fdc_state_write_gap_2:
    break;
  default:
    assert(0);
    break;
  }
}

static void
intel_fdc_check_index_pulse(struct intel_fdc_struct* p_fdc) {
  int was_index_pulse =  p_fdc->state_is_index_pulse;
  p_fdc->state_is_index_pulse = intel_fdc_get_INDEX(p_fdc);

  /* We're only interested in the transition of the pulse going active. */
  if (!p_fdc->state_is_index_pulse || was_index_pulse) {
    return;
  }

  switch (p_fdc->index_pulse_callback) {
  case k_intel_fdc_index_pulse_none:
    break;
  case k_intel_fdc_index_pulse_timeout:
    /* If we see too many index pulses without the progress of a sector,
     * the command times out with $18.
     * EMU: interestingly enough, something like an e.g. 8192 byte sector read
     * times out because such a crazy read hits the default 3 index pulse
     * limit.
     */
    p_fdc->regs[k_intel_fdc_register_internal_index_pulse_count]--;
    if (p_fdc->regs[k_intel_fdc_register_internal_index_pulse_count] == 0) {
      intel_fdc_merge_command_result(p_fdc,
                                     k_intel_fdc_result_sector_not_found);
    }
    break;
  case k_intel_fdc_index_pulse_spindown:
    p_fdc->regs[k_intel_fdc_register_internal_index_pulse_count]--;
    if (p_fdc->regs[k_intel_fdc_register_internal_index_pulse_count] == 0) {
      if (p_fdc->log_commands) {
        log_do_log(k_log_disc, k_log_info, "8271: automatic head unload");
      }
      intel_fdc_drive_out_lower(p_fdc, k_intel_fdc_drive_out_load_head);
      intel_fdc_select_drive(p_fdc, 0);
      p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_none;
    }
    break;
  }
}

static void
intel_fdc_byte_callback(void* p, uint8_t data_byte, uint8_t clocks_byte) {
  uint32_t i;
  int bit;

  struct intel_fdc_struct* p_fdc = (struct intel_fdc_struct*) p;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  uint8_t command = intel_fdc_get_internal_command(p_fdc);

  assert(p_current_drive != NULL);

  intel_fdc_check_index_pulse(p_fdc);

  switch (p_fdc->state) {
  case k_intel_fdc_state_idle:
    /* If the write gate is open outside a command, it cleans flux transitions
     * from the disc surface, effectively creating weak bits!
     */
    if ((p_fdc->drive_out & k_intel_fdc_drive_out_write_enable) &&
        !disc_drive_is_write_protect(p_current_drive)) {
      disc_drive_write_byte(p_current_drive, 0x00, 0x00);
    }
    break;
  case k_intel_fdc_state_seek_setup:
  {
    uint8_t* p_track_regs;
    uint8_t curr_track;
    uint8_t new_track = p_fdc->regs[k_intel_fdc_register_internal_param_1];
    new_track += p_fdc->regs[k_intel_fdc_register_internal_seek_retry_count];

    if (p_fdc->drive_out & k_intel_fdc_drive_out_select_1) {
      p_track_regs = &p_fdc->regs[k_intel_fdc_register_bad_track_1_drive_1];
    } else {
      p_track_regs = &p_fdc->regs[k_intel_fdc_register_bad_track_1_drive_0];
    }
    /* Add one to requested track for each bad track covered. */
    /* EMU NOTE: this is based on a disassembly of the real 8271 ROM and yes,
     * integer overflow does occur!
     */
    if (new_track > 0) {
      if (p_track_regs[0] <= new_track) {
        ++new_track;
      }
      if (p_track_regs[1] <= new_track) {
        ++new_track;
      }
    }
    p_fdc->regs[k_intel_fdc_register_internal_seek_target_1] = new_track;
    p_fdc->regs[k_intel_fdc_register_internal_seek_target_2] = new_track;
    /* Set LOW HEAD CURRENT in drive output depending on track. */
    if (new_track >= 43) {
      p_fdc->drive_out |= k_intel_fdc_drive_out_low_head_current;
    } else {
      p_fdc->drive_out &= ~k_intel_fdc_drive_out_low_head_current;
    }
    /* Work out seek direction and total number of steps. */
    curr_track = p_track_regs[2];
    /* Pretend current track is 255 if a seek to 0. */
    if (new_track == 0) {
      curr_track = 255;
    }
    /* Skip to head load if there's no seek. */
    if (new_track == curr_track) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_settle_or_load_setup);
      break;
    }
    if (new_track > curr_track) {
      p_fdc->regs[k_intel_fdc_register_internal_seek_count] =
          (new_track - curr_track);
      p_fdc->drive_out |= k_intel_fdc_drive_out_direction;
    } else if (curr_track > new_track) {
      p_fdc->regs[k_intel_fdc_register_internal_seek_count] =
          (curr_track - new_track);
      p_fdc->drive_out &= ~k_intel_fdc_drive_out_direction;
    }
    /* Seek pulses out of the 8271 are about 10us, so let's just lower the
     * output bit and make them unobservable as I suspect they are on a real
     * machine.
     */
    p_fdc->drive_out &= ~k_intel_fdc_drive_out_step;
    /* Current track register(s) are updated here before the actual step
     * sequence.
     */
    p_track_regs[2] = p_fdc->regs[k_intel_fdc_register_internal_seek_target_2];
    /* Update both track registers if "single actuator" flag is set. */
    if (p_fdc->regs[k_intel_fdc_register_mode] &
        k_intel_fdc_mode_single_actuator) {
      p_fdc->regs[k_intel_fdc_register_track_drive_0] = p_track_regs[2];
      p_fdc->regs[k_intel_fdc_register_track_drive_1] = p_track_regs[2];
    }
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_seeking);
    break;
  }
  case k_intel_fdc_state_seeking:
  {
    struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
    assert(p_current_drive != NULL);

    if (p_fdc->current_seek_count) {
      p_fdc->current_seek_count--;
      break;
    }

    if ((disc_drive_get_track(p_current_drive) == 0) &&
        (p_fdc->regs[k_intel_fdc_register_internal_seek_target_2] == 0)) {
      /* Seek to 0 done, TRK0 detected. */
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_settle_or_load_setup);
      break;
    } else if (p_fdc->regs[k_intel_fdc_register_internal_seek_count] == 0) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_settle_or_load_setup);
      break;
    }

    p_fdc->regs[k_intel_fdc_register_internal_seek_count]--;
    p_fdc->current_needs_settle = 1;

    if (p_fdc->drive_out & k_intel_fdc_drive_out_direction) {
      disc_drive_seek_track(p_current_drive, 1);
    } else {
      disc_drive_seek_track(p_current_drive, -1);
    }

    /* EMU NOTE: the datasheet is ambiguous about whether the units are 1ms
     * or 2ms for 5.25" drives. 1ms might be your best guess from the
     * datasheet, but timing on a real machine, it appears to be 2ms.
     */
    p_fdc->current_seek_count =
        (p_fdc->regs[k_intel_fdc_register_head_step_rate] * 1000);
    p_fdc->current_seek_count *= 2;
    /* Calculate how many 64us chunks for the head step time. */
    p_fdc->current_seek_count /= 64;

    break;
  }
  case k_intel_fdc_state_settle_or_load_setup:
    assert(p_fdc->current_seek_count == 0);

    if (p_fdc->current_needs_settle) {
      p_fdc->current_needs_settle = 0;
      p_fdc->current_seek_count =
          p_fdc->regs[k_intel_fdc_register_head_settle_time];
      /* EMU: all references state the units are 2ms for 5.25" drives. */
      p_fdc->current_seek_count *= 2;
    }

    /* The head load wait replaces the settle delay if there is both. */
    if (!(p_fdc->drive_out & k_intel_fdc_drive_out_load_head)) {
      intel_fdc_drive_out_raise(p_fdc, k_intel_fdc_drive_out_load_head);
      p_fdc->current_seek_count =
          (p_fdc->regs[k_intel_fdc_register_head_load_unload] & 0x0F);
      /* Head load units are 4ms. */
      p_fdc->current_seek_count *= 4;
    }

    /* Calculate how many 64us chunks for the head settle or load time. */
    p_fdc->current_seek_count *= 1000;
    p_fdc->current_seek_count /= 64;

    intel_fdc_set_state(p_fdc, k_intel_fdc_state_settle_or_load);
    break;
  case k_intel_fdc_state_settle_or_load:
    if (p_fdc->current_seek_count) {
      p_fdc->current_seek_count--;
      break;
    }

    switch (command) {
    case k_intel_fdc_command_READ_ID:
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_wait_no_index);
      break;
    case k_intel_fdc_command_FORMAT:
      intel_fdc_setup_sector_size(p_fdc);
      /* EMU: note that format doesn't set an index pulse timeout. No matter how
       * large the format sector size request, even 16384, the command never
       * exits due to 2 index pulses counted. This differs from read _and_
       * write. Format will exit on the next index pulse after all the sectors
       * have been written.
       * Disc Duplicator III needs this to work correctly when deformatting
       * tracks.
       */
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_wait_no_index);
      break;
    case k_intel_fdc_command_SEEK:
      intel_fdc_merge_command_result(p_fdc, k_intel_fdc_result_ok);
      break;
    default:
      intel_fdc_setup_sector_size(p_fdc);
      intel_fdc_start_index_pulse_timeout(p_fdc);
      intel_fdc_start_syncing_for_header(p_fdc);
      break;
    }

    if (intel_fdc_command_is_writing(p_fdc) && intel_fdc_get_WRPROT(p_fdc)) {
      intel_fdc_merge_command_result(p_fdc,
                                     k_intel_fdc_result_write_protected);
    }
    break;
  case k_intel_fdc_state_wait_no_index:
    if (!p_fdc->state_is_index_pulse) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_wait_index);
    }
    break;
  case k_intel_fdc_state_wait_index:
    if (!p_fdc->state_is_index_pulse) {
      break;
    }
    if (command == k_intel_fdc_command_READ_ID) {
      intel_fdc_start_index_pulse_timeout(p_fdc);
      intel_fdc_start_syncing_for_header(p_fdc);
    } else {
      assert(command == k_intel_fdc_command_FORMAT);
      if (p_fdc->regs[k_intel_fdc_register_internal_param_4] != 0) {
        util_bail("format GAP5 not supported");
      }
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_gap_1);
      /* Need to start writing immediately. */
      intel_fdc_byte_callback_writing(p_fdc);
    }
    break;
  case k_intel_fdc_state_syncing_for_id_wait:
  case k_intel_fdc_state_syncing_for_id:
  case k_intel_fdc_state_check_id_marker:
  case k_intel_fdc_state_in_id:
  case k_intel_fdc_state_in_id_crc:
  case k_intel_fdc_state_syncing_for_data_wait:
  case k_intel_fdc_state_syncing_for_data:
  case k_intel_fdc_state_check_data_marker:
  case k_intel_fdc_state_in_data:
  case k_intel_fdc_state_in_deleted_data:
  case k_intel_fdc_state_in_data_crc:
    /* Switch from a byte stream to a bit stream. This is to cater for HFE
     * files where the bytes are not perfectly aligned to byte boundaries! We
     * do not create any such HFEs but it's easy to get one if you write an
     * HFE in a Gotek.
     */
    for (i = 0; i < 8; ++i) {
      bit = !!(clocks_byte & 0x80);
      intel_fdc_shift_data_bit(p_fdc, bit);
      bit = !!(data_byte & 0x80);
      intel_fdc_shift_data_bit(p_fdc, bit);
      clocks_byte <<= 1;
      data_byte <<= 1;
    }
    break;
  case k_intel_fdc_state_write_gap_2:
  case k_intel_fdc_state_write_sector_data:
  case k_intel_fdc_state_format_gap_1:
  case k_intel_fdc_state_format_write_id:
  case k_intel_fdc_state_format_write_data:
  case k_intel_fdc_state_format_gap_3:
  case k_intel_fdc_state_format_gap_4:
    intel_fdc_byte_callback_writing(p_fdc);
    break;
  default:
    assert(0);
    break;
  }
}

void
intel_fdc_set_drives(struct intel_fdc_struct* p_fdc,
                     struct disc_drive_struct* p_drive_0,
                     struct disc_drive_struct* p_drive_1) {
  assert(p_fdc->p_drive_0 == NULL);
  assert(p_fdc->p_drive_1 == NULL);
  p_fdc->p_drive_0 = p_drive_0;
  p_fdc->p_drive_1 = p_drive_1;

  disc_drive_set_byte_callback(p_drive_0, intel_fdc_byte_callback, p_fdc);
  disc_drive_set_byte_callback(p_drive_1, intel_fdc_byte_callback, p_fdc);
}
