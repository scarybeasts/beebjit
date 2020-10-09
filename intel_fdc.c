#include "intel_fdc.h"

#include "bbc_options.h"
#include "disc_drive.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "state_6502.h"
#include "timing.h"
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
  k_intel_fdc_index_pulse_start_read_id = 4,
  k_intel_fdc_index_pulse_start_format = 5,
  k_intel_fdc_index_pulse_stop_format = 6,
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
  k_intel_fdc_result_drive_not_ready = 0x10,
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
  k_intel_fdc_register_internal_ms_count_hi = 0x08,
  k_intel_fdc_register_internal_ms_count_lo = 0x09,
  k_intel_fdc_register_internal_seek_count = 0x0A,
  k_intel_fdc_register_internal_id_sector = 0x0A,
  k_intel_fdc_register_internal_seek_target_1 = 0x0B,
  k_intel_fdc_register_internal_dynamic_dispatch = 0x0B,
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
  k_intel_fdc_register_internal_drive_in_copy = 0x15,
  k_intel_fdc_register_internal_write_run_data = 0x15,
  k_intel_fdc_register_internal_gap2_skip = 0x15,
  k_intel_fdc_register_internal_result = 0x16,
  k_intel_fdc_register_mode = 0x17,
  k_intel_fdc_register_internal_status = 0x17,
  k_intel_fdc_register_bad_track_1_drive_1 = 0x18,
  k_intel_fdc_register_bad_track_2_drive_1 = 0x19,
  k_intel_fdc_register_track_drive_1 = 0x1A,
  k_intel_fdc_register_internal_drive_in_latched = 0x1B,
  k_intel_fdc_register_internal_index_pulse_count = 0x1C,
  k_intel_fdc_register_internal_data = 0x1D,
  k_intel_fdc_register_internal_parameter = 0x1E,
  k_intel_fdc_register_internal_command = 0x1F,
  k_intel_fdc_register_drive_in = 0x22,
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
  k_intel_fdc_state_syncing_for_id_wait,
  k_intel_fdc_state_syncing_for_id,
  k_intel_fdc_state_check_id_marker,
  k_intel_fdc_state_in_id,
  k_intel_fdc_state_in_id_crc,
  k_intel_fdc_state_syncing_for_data,
  k_intel_fdc_state_check_data_marker,
  k_intel_fdc_state_in_data,
  k_intel_fdc_state_in_data_crc,
  k_intel_fdc_state_skip_gap_2,
  k_intel_fdc_state_write_run,
  k_intel_fdc_state_write_data_mark,
  k_intel_fdc_state_write_sector_data,
  k_intel_fdc_state_write_crc_2,
  k_intel_fdc_state_write_crc_3,
  k_intel_fdc_state_dynamic_dispatch,
  k_intel_fdc_state_format_write_id_marker,
  k_intel_fdc_state_format_id_crc_2,
  k_intel_fdc_state_format_id_crc_3,
  k_intel_fdc_state_format_write_data_marker,
  k_intel_fdc_state_format_data_crc_2,
  k_intel_fdc_state_format_data_crc_3,
  k_intel_fdc_state_format_gap_4,
};

enum {
  k_intel_fdc_timer_none = 0,
  k_intel_fdc_timer_seek_step = 1,
  k_intel_fdc_timer_post_seek = 2,
};

enum {
  k_intel_fdc_call_unchanged = 1,
  k_intel_fdc_call_seek = 2,
  k_intel_fdc_call_read_id = 3,
  k_intel_fdc_call_read = 4,
  k_intel_fdc_call_write = 5,
  k_intel_fdc_call_format = 6,
  k_intel_fdc_call_format_GAP1_or_GAP3_FFs = 7,
  k_intel_fdc_call_format_GAP1_or_GAP3_00s = 8,
  k_intel_fdc_call_format_GAP2_FFs = 9,
  k_intel_fdc_call_format_GAP2_00s = 10,
  k_intel_fdc_call_format_data = 11,
};

enum {
  k_intel_num_registers = 32,
};

struct intel_fdc_struct {
  struct state_6502* p_state_6502;
  struct timing_struct* p_timing;
  uint32_t timer_id;

  int log_commands;

  struct disc_drive_struct* p_drive_0;
  struct disc_drive_struct* p_drive_1;
  struct disc_drive_struct* p_current_drive;

  /* Event callbacks and execution contexts. */
  int parameter_callback;
  int index_pulse_callback;
  int timer_state;
  int call_context;

  uint8_t regs[k_intel_num_registers];
  int is_result_ready;
  uint8_t mmio_data;
  uint8_t mmio_clocks;
  uint8_t drive_out;

  uint32_t shift_register;
  uint32_t num_shifts;

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

static inline uint8_t
intel_fdc_get_external_status(struct intel_fdc_struct* p_fdc) {
  uint8_t status = intel_fdc_get_status(p_fdc);
  /* The internal status register appears to be shared with some mode bits that
   * must be masked out.
   */
  status &= ~0x03;
  /* Current best thinking is that the internal register uses bit value 0x10 for
   * something different, and that "result ready" is maintained by the external
   * register logic.
   */
  status &= ~k_intel_fdc_status_flag_result_ready;
  if (p_fdc->is_result_ready) {
    status |= k_intel_fdc_status_flag_result_ready;
  }

  /* TODO: "command register full", bit value 0x40, isn't understood. In
   * particular, the mode register (shared with the status register we
   * believe) is set to 0xC1 in typical operation. This would seem to raise
   * 0x40 after it has been lowered at command register acceptance. However,
   * the bit is not returned.
   * Don't return it, ever, for now.
   */
  /* Also avoid "parameter register full". */
  status &= ~0x60;

  return status;
}

static void
intel_fdc_update_nmi(struct intel_fdc_struct* p_fdc) {
  struct state_6502* p_state_6502 = p_fdc->p_state_6502;
  uint8_t status = intel_fdc_get_status(p_fdc);
  int level = !!(status & k_intel_fdc_status_flag_nmi);
  int firing = state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi);

  if (firing && (level == 1)) {
    log_do_log(k_log_disc, k_log_error, "edge triggered NMI already high");
  }

  state_6502_set_irq_level(p_state_6502, k_state_6502_irq_nmi, level);
}

static inline void
intel_fdc_status_raise(struct intel_fdc_struct* p_fdc, uint8_t bits) {
  p_fdc->regs[k_intel_fdc_register_internal_status] |= bits;
  if (bits & k_intel_fdc_status_flag_nmi) {
    intel_fdc_update_nmi(p_fdc);
  }
}

static inline void
intel_fdc_status_lower(struct intel_fdc_struct* p_fdc, uint8_t bits) {
  p_fdc->regs[k_intel_fdc_register_internal_status] &= ~bits;
  if (bits & k_intel_fdc_status_flag_nmi) {
    intel_fdc_update_nmi(p_fdc);
  }
}

static inline uint8_t
intel_fdc_get_result(struct intel_fdc_struct* p_fdc) {
  return p_fdc->regs[k_intel_fdc_register_internal_result];
}

static void
intel_fdc_set_result(struct intel_fdc_struct* p_fdc, uint8_t result) {
  p_fdc->regs[k_intel_fdc_register_internal_result] = result;
  p_fdc->is_result_ready = 1;
}

static inline uint8_t
intel_fdc_get_internal_command(struct intel_fdc_struct* p_fdc) {
  uint8_t command = p_fdc->regs[k_intel_fdc_register_internal_command];
  command &= 0x3C;
  return (command >> 2);
}

static inline uint32_t
intel_fdc_get_sector_size(struct intel_fdc_struct* p_fdc) {
  uint32_t size = (p_fdc->regs[k_intel_fdc_register_internal_param_3] >> 5);
  size = (128 << size);
  return size;
}

static void
intel_fdc_setup_sector_size(struct intel_fdc_struct* p_fdc) {
  uint32_t size = intel_fdc_get_sector_size(p_fdc);
  uint32_t msb = ((size / 128) - 1);
  p_fdc->regs[k_intel_fdc_register_internal_count_lsb] = 0x80;
  p_fdc->regs[k_intel_fdc_register_internal_count_msb] = msb;
  /* NOTE: this is R0, i.e. R0 is trashed here. */
  p_fdc->regs[k_intel_fdc_register_internal_count_msb_copy] = msb;
}

static void
intel_fdc_start_irq_callbacks(struct intel_fdc_struct* p_fdc) {
  p_fdc->regs[k_intel_fdc_register_internal_status] |= 0x30;
}

static void
intel_fdc_stop_irq_callbacks(struct intel_fdc_struct* p_fdc) {
  p_fdc->regs[k_intel_fdc_register_internal_status] &= ~0x30;
}

static int
intel_fdc_is_irq_callbacks(struct intel_fdc_struct* p_fdc) {
  if ((p_fdc->regs[k_intel_fdc_register_internal_status] & 0x30) == 0x30) {
    return 1;
  }
  return 0;
}

static int
intel_fdc_decrement_counter(struct intel_fdc_struct* p_fdc) {
  p_fdc->regs[k_intel_fdc_register_internal_count_lsb]--;
  if (p_fdc->regs[k_intel_fdc_register_internal_count_lsb] != 0) {
    return 0;
  }
  p_fdc->regs[k_intel_fdc_register_internal_count_msb]--;
  if (p_fdc->regs[k_intel_fdc_register_internal_count_msb] != 0xFF) {
    p_fdc->regs[k_intel_fdc_register_internal_count_lsb] = 0x80;
    return 0;
  }
  p_fdc->regs[k_intel_fdc_register_internal_count_msb] = 0;
  intel_fdc_stop_irq_callbacks(p_fdc);
  return 1;
}

static void
intel_fdc_start_index_pulse_timeout(struct intel_fdc_struct* p_fdc) {
  p_fdc->regs[k_intel_fdc_register_internal_index_pulse_count] = 3;
  p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_timeout;
}

static void
intel_fdc_set_drive_out(struct intel_fdc_struct* p_fdc, uint8_t drive_out) {
  uint8_t select_bits;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;

  if (p_current_drive != NULL) {
    if (p_fdc->drive_out & k_intel_fdc_drive_out_load_head) {
      disc_drive_stop_spinning(p_current_drive);
    }
    p_current_drive = NULL;
  }

  /* NOTE: unclear what to do if both drives are selected. We select no drive
   * for now, to avoid shenanigans.
   */
  select_bits = (drive_out & 0xC0);
  if (select_bits == 0x40) {
    p_current_drive = p_fdc->p_drive_0;
  } else if (select_bits == 0x80) {
    p_current_drive = p_fdc->p_drive_1;
  }

  p_fdc->p_current_drive = p_current_drive;

  if (p_current_drive != NULL) {
    if (drive_out & k_intel_fdc_drive_out_load_head) {
      disc_drive_start_spinning(p_current_drive);
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
intel_fdc_set_state(struct intel_fdc_struct* p_fdc, int state) {
  p_fdc->state = state;
  p_fdc->state_count = 0;

  if ((state == k_intel_fdc_state_syncing_for_id) ||
      (state == k_intel_fdc_state_syncing_for_data)) {
    p_fdc->shift_register = 0;
    p_fdc->num_shifts = 0;
  }
}

static void
intel_fdc_clear_callbacks(struct intel_fdc_struct* p_fdc) {
  p_fdc->parameter_callback = k_intel_fdc_parameter_accept_none;
  p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_none;
  if (p_fdc->timer_state != k_intel_fdc_timer_none) {
    (void) timing_stop_timer(p_fdc->p_timing, p_fdc->timer_id);
    p_fdc->timer_state = k_intel_fdc_timer_none;
  }
  /* Think of this as the read / write callback from the bit processor. */
  intel_fdc_set_state(p_fdc, k_intel_fdc_state_idle);
}

static void
intel_fdc_lower_busy_and_log(struct intel_fdc_struct* p_fdc) {
  intel_fdc_status_lower(p_fdc, k_intel_fdc_status_flag_busy);

  if (p_fdc->log_commands) {
    log_do_log(k_log_disc,
               k_log_info,
               "8271: status $%x result $%x",
               intel_fdc_get_external_status(p_fdc),
               intel_fdc_get_result(p_fdc));
  }
}

static void
intel_fdc_spindown(struct intel_fdc_struct* p_fdc) {
  intel_fdc_drive_out_lower(p_fdc,
                            (k_intel_fdc_drive_out_select_1 |
                                 k_intel_fdc_drive_out_select_0 |
                                 k_intel_fdc_drive_out_load_head));
}

static void
intel_fdc_finish_simple_command(struct intel_fdc_struct* p_fdc) {
  uint8_t head_unload_count;

  intel_fdc_lower_busy_and_log(p_fdc);
  intel_fdc_stop_irq_callbacks(p_fdc);
  intel_fdc_clear_callbacks(p_fdc);

  head_unload_count = (p_fdc->regs[k_intel_fdc_register_head_load_unload] >> 4);

  if (head_unload_count == 0) {
    /* Unload immediately. */
    intel_fdc_spindown(p_fdc);
  } else if (head_unload_count == 0xF) {
    /* Never automatically unload. */
  } else {
    p_fdc->regs[k_intel_fdc_register_internal_index_pulse_count] =
        head_unload_count;
    p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_spindown;
  }
}

static void
intel_fdc_finish_command(struct intel_fdc_struct* p_fdc, uint8_t result) {
  if (result != k_intel_fdc_result_ok) {
    intel_fdc_drive_out_lower(p_fdc, (k_intel_fdc_drive_out_direction |
                                          k_intel_fdc_drive_out_step |
                                          k_intel_fdc_drive_out_write_enable));
  }
  result |= intel_fdc_get_result(p_fdc);
  intel_fdc_set_result(p_fdc, result);
  /* Raise command completion IRQ. */
  intel_fdc_status_raise(p_fdc, k_intel_fdc_status_flag_nmi);
  intel_fdc_finish_simple_command(p_fdc);
}

static void
intel_fdc_command_abort(struct intel_fdc_struct* p_fdc) {
  /* If we're aborting a command in the middle of writing data, it usually
   * doesn't leave a clean byte end on the disc. This is not particularly
   * important to emulate at all but it does help create new copy protection
   * schemes under emulation.
   */
  if (p_fdc->drive_out & k_intel_fdc_drive_out_write_enable) {
    uint32_t pulses = ibm_disc_format_fm_to_2us_pulses(0xFF, 0xFF);
    disc_drive_write_pulses(p_fdc->p_current_drive, pulses);
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
  assert(p_fdc->index_pulse_callback == k_intel_fdc_index_pulse_none);
  assert(p_fdc->timer_state == k_intel_fdc_timer_none);
  assert(p_fdc->state == k_intel_fdc_state_idle);
  assert(p_fdc->p_current_drive == NULL);
  assert(p_fdc->drive_out == 0);

  (void) memset(&p_fdc->regs[0], '\0', sizeof(p_fdc->regs));
  p_fdc->is_result_ready = 0;
  p_fdc->mmio_data = 0;
  p_fdc->mmio_clocks = 0;
  p_fdc->state_count = 0;
  p_fdc->state_is_index_pulse = 0;
}

void
intel_fdc_break_reset(struct intel_fdc_struct* p_fdc) {
  /* Abort any in-progress command. */
  intel_fdc_command_abort(p_fdc);
  intel_fdc_clear_callbacks(p_fdc);

  /* Deselect any drive; ensures spin-down. */
  intel_fdc_set_drive_out(p_fdc, 0);

  /* EMU: on a real machine, status appears to be cleared but result and data
   * register not. */
  intel_fdc_status_lower(p_fdc, intel_fdc_get_status(p_fdc));
}

static void
intel_fdc_start_syncing_for_header(struct intel_fdc_struct* p_fdc) {
  p_fdc->regs[k_intel_fdc_register_internal_header_pointer] = 0x0C;
  intel_fdc_set_state(p_fdc, k_intel_fdc_state_syncing_for_id);
}

static void
intel_fdc_set_timer_ms(struct intel_fdc_struct* p_fdc,
                       int timer_state,
                       uint32_t wait_ms) {
  struct timing_struct* p_timing = p_fdc->p_timing;
  uint32_t timer_id = p_fdc->timer_id;

  if (timing_timer_is_running(p_fdc->p_timing, timer_id)) {
    (void) timing_stop_timer(p_timing, timer_id);
  }

  p_fdc->timer_state = timer_state;
  (void) timing_start_timer_with_value(p_timing, timer_id, (wait_ms * 2000));
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
intel_fdc_read_drive_in(struct intel_fdc_struct* p_fdc) {
  /* EMU NOTE: on my machine, bit 7 and bit 0 appear to be set all the time. */
  uint8_t drive_in = 0x81;
  if (intel_fdc_current_disc_is_spinning(p_fdc)) {
    if (intel_fdc_get_TRK0(p_fdc)) {
      /* TRK0 */
      drive_in |= 0x02;
    }
    if (p_fdc->drive_out & 0x40) {
      /* RDY0 */
      drive_in |= 0x04;
    }
    if (p_fdc->drive_out & 0x80) {
      /* RDY1 */
      drive_in |= 0x40;
    }
    if (intel_fdc_get_WRPROT(p_fdc)) {
      /* WR PROT */
      drive_in |= 0x08;
    }
    if (intel_fdc_get_INDEX(p_fdc)) {
      /* INDEX */
      drive_in |= 0x10;
    }
  }
  return drive_in;
}

static uint8_t
intel_fdc_do_read_drive_status(struct intel_fdc_struct* p_fdc) {
  uint8_t drive_in = intel_fdc_read_drive_in(p_fdc);
  p_fdc->regs[k_intel_fdc_register_internal_drive_in_copy] = drive_in;
  p_fdc->regs[k_intel_fdc_register_internal_drive_in_latched] |= 0xBB;
  drive_in &= p_fdc->regs[k_intel_fdc_register_internal_drive_in_latched];
  p_fdc->regs[k_intel_fdc_register_internal_drive_in_latched] = drive_in;
  return drive_in;
}

static int
intel_fdc_check_drive_ready(struct intel_fdc_struct* p_fdc) {
  uint8_t mask;

  (void) intel_fdc_do_read_drive_status(p_fdc);

  if (p_fdc->drive_out & k_intel_fdc_drive_out_select_1) {
    mask = 0x40;
  } else {
    mask = 0x04;
  }

  if (!(p_fdc->regs[k_intel_fdc_register_internal_drive_in_latched] & mask)) {
    intel_fdc_finish_command(p_fdc, k_intel_fdc_result_drive_not_ready);
    return 0;
  }

  return 1;
}

static void
intel_fdc_check_write_protect(struct intel_fdc_struct* p_fdc) {
  if (p_fdc->regs[k_intel_fdc_register_internal_drive_in_latched] & 0x08) {
    intel_fdc_finish_command(p_fdc, k_intel_fdc_result_write_protected);
  }
}

static void
intel_fdc_post_seek_dispatch(struct intel_fdc_struct* p_fdc) {
  p_fdc->timer_state = k_intel_fdc_timer_none;

  if (!intel_fdc_check_drive_ready(p_fdc)) {
    return;
  }

  switch (p_fdc->call_context) {
  case k_intel_fdc_call_seek:
    intel_fdc_finish_command(p_fdc, k_intel_fdc_result_ok);
    break;
  case k_intel_fdc_call_read_id:
    p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_start_read_id;
    break;
  case k_intel_fdc_call_format:
    intel_fdc_setup_sector_size(p_fdc);
    p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_start_format;
    intel_fdc_check_write_protect(p_fdc);
    break;
  case k_intel_fdc_call_read:
  case k_intel_fdc_call_write:
    intel_fdc_setup_sector_size(p_fdc);
    intel_fdc_start_index_pulse_timeout(p_fdc);
    intel_fdc_start_syncing_for_header(p_fdc);
    if (p_fdc->call_context == k_intel_fdc_call_write) {
      intel_fdc_check_write_protect(p_fdc);
    }
    break;
  default:
    assert(0);
    break;
  }
}

static void
intel_fdc_do_load_head(struct intel_fdc_struct* p_fdc, int is_settle) {
  uint32_t post_seek_time = 0;

  /* The head load wait replaces the settle delay if there is both. */
  if (!(p_fdc->drive_out & k_intel_fdc_drive_out_load_head)) {
    intel_fdc_drive_out_raise(p_fdc, k_intel_fdc_drive_out_load_head);
    post_seek_time =
        (p_fdc->regs[k_intel_fdc_register_head_load_unload] & 0x0F);
    /* Head load units are 4ms. */
    post_seek_time *= 4;
  } else if (is_settle) {
    /* EMU: all references state the units are 2ms for 5.25" drives. */
    post_seek_time = (p_fdc->regs[k_intel_fdc_register_head_settle_time] * 2);
  }

  if (post_seek_time > 0) {
    intel_fdc_set_timer_ms(p_fdc, k_intel_fdc_timer_post_seek, post_seek_time);
  } else {
    intel_fdc_post_seek_dispatch(p_fdc);
  }
}

static void
intel_fdc_do_seek_step(struct intel_fdc_struct* p_fdc) {
  uint32_t step_rate;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;
  assert(p_current_drive != NULL);

  if ((disc_drive_get_track(p_current_drive) == 0) &&
      (p_fdc->regs[k_intel_fdc_register_internal_seek_target_2] == 0)) {
    /* Seek to 0 done, TRK0 detected. */
    intel_fdc_do_load_head(p_fdc, 1);
    return;
  } else if (p_fdc->regs[k_intel_fdc_register_internal_seek_count] == 0) {
    intel_fdc_do_load_head(p_fdc, 1);
    return;
  }

  p_fdc->regs[k_intel_fdc_register_internal_seek_count]--;

  if (p_fdc->drive_out & k_intel_fdc_drive_out_direction) {
    disc_drive_seek_track(p_current_drive, 1);
  } else {
    disc_drive_seek_track(p_current_drive, -1);
  }

  step_rate = p_fdc->regs[k_intel_fdc_register_head_step_rate];
  if (step_rate == 0) {
    util_bail("drive timed seek not handled");
  }

  /* EMU NOTE: the datasheet is ambiguous about whether the units are 1ms
   * or 2ms for 5.25" drives. 1ms might be your best guess from the
   * datasheet, but timing on a real machine, it appears to be 2ms.
   */
  intel_fdc_set_timer_ms(p_fdc, k_intel_fdc_timer_seek_step, (step_rate * 2));
}

static void
intel_fdc_timer_fired(void* p) {
  struct intel_fdc_struct* p_fdc = (struct intel_fdc_struct*) p;

  (void) timing_stop_timer(p_fdc->p_timing, p_fdc->timer_id);

  /* Counting milliseconds is done with R8 and R9, which are left at zero
   * after a busy wait.
   */
  p_fdc->regs[k_intel_fdc_register_internal_ms_count_hi] = 0;
  p_fdc->regs[k_intel_fdc_register_internal_ms_count_lo] = 0;

  switch (p_fdc->timer_state) {
  case k_intel_fdc_timer_seek_step:
    intel_fdc_do_seek_step(p_fdc);
    break;
  case k_intel_fdc_timer_post_seek:
    intel_fdc_post_seek_dispatch(p_fdc);
    break;
  default:
    assert(0);
    break;
  }
}

struct intel_fdc_struct*
intel_fdc_create(struct state_6502* p_state_6502,
                 struct timing_struct* p_timing,
                 struct bbc_options* p_options) {
  struct intel_fdc_struct* p_fdc =
      util_mallocz(sizeof(struct intel_fdc_struct));

  p_fdc->p_state_6502 = p_state_6502;
  p_fdc->p_timing = p_timing;
  p_fdc->timer_id = timing_register_timer(p_timing,
                                          intel_fdc_timer_fired,
                                          p_fdc);

  p_fdc->log_commands = util_has_option(p_options->p_log_flags,
                                        "disc:commands");

  intel_fdc_clear_callbacks(p_fdc);

  return p_fdc;
}

void
intel_fdc_destroy(struct intel_fdc_struct* p_fdc) {
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

uint8_t
intel_fdc_read(struct intel_fdc_struct* p_fdc, uint16_t addr) {
  uint8_t result;

  switch (addr & 0x07) {
  case k_intel_fdc_status:
    return intel_fdc_get_external_status(p_fdc);
  case k_intel_fdc_result:
    result = intel_fdc_get_result(p_fdc);
    p_fdc->is_result_ready = 0;
    intel_fdc_status_lower(p_fdc, k_intel_fdc_status_flag_nmi);
    return result;
  /* EMU: on a real model B, the i8271 has the data register mapped for all of
   * register address 4 - 7.
   */
  case k_intel_fdc_data:
  case (k_intel_fdc_data + 1):
  case (k_intel_fdc_data + 2):
  case (k_intel_fdc_data + 3):
    result = intel_fdc_get_result(p_fdc);
    intel_fdc_status_lower(p_fdc, (k_intel_fdc_status_flag_need_data |
                                       k_intel_fdc_status_flag_nmi));
    return p_fdc->regs[k_intel_fdc_register_internal_data];
  /* EMU: register address 2 and 3 are not documented as having anything
   * wired up for reading, BUT on a model B, they appear to give the MSB and
   * LSB of the sector byte counter in internal registers 19 ($13) and 20 ($14).
   */
  case k_intel_fdc_unknown_read_2:
    return p_fdc->regs[k_intel_fdc_register_internal_count_msb];
  case k_intel_fdc_unknown_read_3:
    return p_fdc->regs[k_intel_fdc_register_internal_count_lsb];
  default:
    assert(0);
    return 0;
  }
}

static uint8_t
intel_fdc_read_register(struct intel_fdc_struct* p_fdc, uint8_t reg) {
  uint8_t ret = 0;

  reg = (reg & 0x3F);
  if (reg < k_intel_num_registers) {
    return p_fdc->regs[reg];
  }
  reg = (reg & 0x07);
  switch (reg) {
  case (k_intel_fdc_register_drive_in & 0x07):
    ret = intel_fdc_read_drive_in(p_fdc);
    break;
  case (k_intel_fdc_register_drive_out & 0x07):
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
intel_fdc_do_seek(struct intel_fdc_struct* p_fdc, int call_context) {
  uint8_t* p_track_regs;
  uint8_t curr_track;
  uint8_t new_track;

  if (call_context != k_intel_fdc_call_unchanged) {
    p_fdc->call_context = call_context;
  }

  new_track = p_fdc->regs[k_intel_fdc_register_internal_param_1];
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
    intel_fdc_do_load_head(p_fdc, 0);
    return;
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

  intel_fdc_do_seek_step(p_fdc);
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
    temp_u8 = intel_fdc_do_read_drive_status(p_fdc);
    intel_fdc_set_result(p_fdc, temp_u8);
    p_fdc->regs[k_intel_fdc_register_internal_drive_in_latched] =
        p_fdc->regs[k_intel_fdc_register_internal_drive_in_copy];
    intel_fdc_finish_simple_command(p_fdc);
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
    /* WRITE_SPECIAL_REGISTER tidies up in a much simpler way than other
     * commands.
     */
    intel_fdc_lower_busy_and_log(p_fdc);
    break;
  case k_intel_fdc_command_READ_SPECIAL_REGISTER:
    temp_u8 = intel_fdc_read_register(
        p_fdc, p_fdc->regs[k_intel_fdc_register_internal_param_1]);
    intel_fdc_set_result(p_fdc, temp_u8);
    intel_fdc_finish_simple_command(p_fdc);
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
      intel_fdc_do_seek(p_fdc, k_intel_fdc_call_read_id);
    } else {
      intel_fdc_start_syncing_for_header(p_fdc);
    }
    break;
  case k_intel_fdc_command_SEEK:
    intel_fdc_do_seek(p_fdc, k_intel_fdc_call_seek);
    break;
  case k_intel_fdc_command_READ_DATA:
  case k_intel_fdc_command_READ_DATA_AND_DELETED:
  case k_intel_fdc_command_VERIFY:
  case k_intel_fdc_command_SCAN_DATA:
  case k_intel_fdc_command_SCAN_DATA_AND_DELETED:
    intel_fdc_do_seek(p_fdc, k_intel_fdc_call_read);
    break;
  case k_intel_fdc_command_WRITE_DATA:
    p_fdc->regs[k_intel_fdc_register_internal_param_data_marker] =
        k_ibm_disc_data_mark_data_pattern;
    intel_fdc_do_seek(p_fdc, k_intel_fdc_call_write);
    break;
  case k_intel_fdc_command_WRITE_DELETED_DATA:
    p_fdc->regs[k_intel_fdc_register_internal_param_data_marker] =
        k_ibm_disc_deleted_data_mark_data_pattern;
    intel_fdc_do_seek(p_fdc, k_intel_fdc_call_write);
    break;
  case k_intel_fdc_command_FORMAT:
    intel_fdc_do_seek(p_fdc, k_intel_fdc_call_format);
    break;
  default:
    assert(0);
    break;
  }
}

static void
intel_fdc_start_command(struct intel_fdc_struct* p_fdc) {
  uint8_t command;
  uint8_t select_bits;

  uint8_t command_reg = p_fdc->regs[k_intel_fdc_register_internal_command];
  uint8_t orig_command = command_reg;

  /* This updates R21 ($15) and R27 ($1B). R27 is later referenced for checking
   * the write protect bit.
   */
  (void) intel_fdc_do_read_drive_status(p_fdc);

  p_fdc->parameter_callback = k_intel_fdc_parameter_accept_none;

  /* Select the drive before logging so that head position is reported. */
  /* The MMIO clocks register really is used as temporary storage for this. */
  p_fdc->mmio_clocks = (command_reg & 0xC0);
  if (p_fdc->mmio_clocks != (p_fdc->drive_out & 0xC0)) {
    /* A change of drive select bits clears all drive out bits other than side
     * select.
     * For example, the newly selected drive won't have the load head signal
     * active. This spins down any previously selected drive.
     */
    select_bits = p_fdc->mmio_clocks;
    select_bits |= (p_fdc->drive_out & k_intel_fdc_drive_out_side);
    intel_fdc_set_drive_out(p_fdc, select_bits);
  }

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
               select_bits,
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
  /* Set busy, lower command full in status, result to 0. */
  intel_fdc_status_raise(p_fdc, k_intel_fdc_status_flag_busy);
  intel_fdc_status_lower(p_fdc, k_intel_fdc_status_flag_command_full);
  intel_fdc_set_result(p_fdc, 0);

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
  /* From testing, writing parameter appears to clear "result ready". */
  p_fdc->is_result_ready = 0;

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
    if (p_fdc->log_commands) {
      log_do_log(k_log_disc,
                 k_log_info,
                 "8271: specify param $%x",
                 p_fdc->regs[k_intel_fdc_register_internal_parameter]);
    }
    intel_fdc_write_register(
        p_fdc,
        p_fdc->regs[k_intel_fdc_register_internal_pointer],
        p_fdc->regs[k_intel_fdc_register_internal_parameter]);
    p_fdc->regs[k_intel_fdc_register_internal_pointer]++;
    p_fdc->regs[k_intel_fdc_register_internal_param_count]--;
    if (p_fdc->regs[k_intel_fdc_register_internal_param_count] == 0) {
      intel_fdc_finish_simple_command(p_fdc);
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
    intel_fdc_status_lower(p_fdc, (k_intel_fdc_status_flag_need_data |
                                       k_intel_fdc_status_flag_nmi));
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
  intel_fdc_finish_command(p_fdc, k_intel_fdc_result_late_dma);

  return 0;
}

static int
intel_fdc_check_crc(struct intel_fdc_struct* p_fdc, uint8_t error) {
  if (p_fdc->crc == p_fdc->on_disc_crc) {
    return 1;
  }

  intel_fdc_finish_command(p_fdc, error);
  return 0;
}

static void
intel_fdc_check_completion(struct intel_fdc_struct* p_fdc) {
  if (!intel_fdc_check_drive_ready(p_fdc)) {
    return;
  }

  /* Lower WRITE_ENABLE. */
  intel_fdc_drive_out_lower(p_fdc, k_intel_fdc_drive_out_write_enable);
  intel_fdc_clear_callbacks(p_fdc);

  /* One less sector to go. */
  /* Specifying 0 sectors seems to result in 32 read, due to underflow of the
   * 5-bit counter.
   * On commands other than READ_ID, any underflow has other side effects
   * such as modifying the sector size.
   */
  p_fdc->regs[k_intel_fdc_register_internal_param_3]--;
  if ((p_fdc->regs[k_intel_fdc_register_internal_param_3] & 0x1F) == 0) {
    intel_fdc_finish_command(p_fdc, k_intel_fdc_result_ok);
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
intel_fdc_do_write_run(struct intel_fdc_struct* p_fdc,
                       int call_context,
                       uint8_t byte) {
  p_fdc->mmio_data = byte;
  p_fdc->mmio_clocks = 0xFF;
  p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, p_fdc->mmio_data);
  p_fdc->regs[k_intel_fdc_register_internal_write_run_data] = byte;
  intel_fdc_drive_out_raise(p_fdc, k_intel_fdc_drive_out_write_enable);
  p_fdc->call_context = call_context;
  intel_fdc_set_state(p_fdc, k_intel_fdc_state_write_run);
}

static void
intel_fdc_byte_callback_reading(struct intel_fdc_struct* p_fdc,
                                uint8_t data_byte,
                                uint8_t clocks_byte) {
  int is_done;
  uint8_t command = intel_fdc_get_internal_command(p_fdc);

  if (intel_fdc_is_irq_callbacks(p_fdc)) {
    if (!intel_fdc_check_data_loss_ok(p_fdc)) {
      return;
    }
    p_fdc->regs[k_intel_fdc_register_internal_data] = data_byte;
    intel_fdc_status_raise(p_fdc, (k_intel_fdc_status_flag_nmi |
                                       k_intel_fdc_status_flag_need_data));
  }

  switch (p_fdc->state) {
  case k_intel_fdc_state_skip_gap_2:
    /* The controller requires a minimum byte count of 12 before sync then
     * sector data. 2 bytes of sync are needed, so absolute minumum gap here is
     * 14. The controller formats to 17 (not user controllable).
     */
    /* TODO: the controller enforced gap skip is 11 bytes of read, as per the
     * ROM. The practical count of 12 is likely because the controller takes
     * some number of microseconds to start the sync detector after this
     * counter expires.
     */
    p_fdc->regs[k_intel_fdc_register_internal_gap2_skip]--;
    if (p_fdc->regs[k_intel_fdc_register_internal_gap2_skip] != 0) {
      break;
    }
    switch (p_fdc->call_context) {
    case k_intel_fdc_call_read:
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_syncing_for_data);
      break;
    case k_intel_fdc_call_write:
      intel_fdc_do_write_run(p_fdc, k_intel_fdc_call_write, 0x00);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_intel_fdc_state_check_id_marker:
    if ((clocks_byte == k_ibm_disc_mark_clock_pattern) &&
        (data_byte == k_ibm_disc_id_mark_data_pattern)) {
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc =
          ibm_disc_format_crc_add_byte(p_fdc->crc,
                                       k_ibm_disc_id_mark_data_pattern);

      if (command == k_intel_fdc_command_READ_ID) {
        intel_fdc_start_irq_callbacks(p_fdc);
      }
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_id);
    } else {
      intel_fdc_start_syncing_for_header(p_fdc);
    }
    break;
  case k_intel_fdc_state_in_id:
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
    intel_fdc_write_register(
        p_fdc,
        p_fdc->regs[k_intel_fdc_register_internal_header_pointer],
        data_byte);
    p_fdc->regs[k_intel_fdc_register_internal_header_pointer]--;
    if ((p_fdc->regs[k_intel_fdc_register_internal_header_pointer] & 0x07) ==
            0) {
      p_fdc->on_disc_crc = 0;
      intel_fdc_stop_irq_callbacks(p_fdc);
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
      /* This is a test for the READ_ID command. */
      if (p_fdc->regs[k_intel_fdc_register_internal_command] == 0x18) {
        intel_fdc_check_completion(p_fdc);
      } else if (p_fdc->regs[k_intel_fdc_register_internal_id_track] !=
                 p_fdc->regs[k_intel_fdc_register_internal_param_1]) {
        /* EMU NOTE: upon any mismatch of found track vs. expected track,
         * the drive will try twice more on the next two tracks.
         */
        p_fdc->regs[k_intel_fdc_register_internal_seek_retry_count]++;
        if (p_fdc->regs[k_intel_fdc_register_internal_seek_retry_count] == 3) {
          intel_fdc_finish_command(p_fdc, k_intel_fdc_result_sector_not_found);
        } else {
          intel_fdc_do_seek(p_fdc, k_intel_fdc_call_unchanged);
        }
      } else if (p_fdc->regs[k_intel_fdc_register_internal_id_sector] ==
                 p_fdc->regs[k_intel_fdc_register_internal_param_2]) {
        p_fdc->regs[k_intel_fdc_register_internal_gap2_skip] = 11;
        if (p_fdc->call_context == k_intel_fdc_call_write) {
          /* Set up for the first 5 bytes of the 6 bytes of 0x00 sync. */
          p_fdc->regs[k_intel_fdc_register_internal_count_msb] = 0;
          p_fdc->regs[k_intel_fdc_register_internal_count_lsb] = 5;
        }
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_skip_gap_2);
      } else {
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_syncing_for_id_wait);
      }
    }
    break;
  case k_intel_fdc_state_check_data_marker:
    if ((clocks_byte == k_ibm_disc_mark_clock_pattern) &&
        ((data_byte == k_ibm_disc_data_mark_data_pattern) ||
            (data_byte == k_ibm_disc_deleted_data_mark_data_pattern))) {
      int do_irqs = 1;
      if (data_byte == k_ibm_disc_deleted_data_mark_data_pattern) {
        if ((p_fdc->regs[k_intel_fdc_register_internal_command] & 0x04) == 0) {
          do_irqs = 0;
        }
        intel_fdc_set_result(p_fdc, k_intel_fdc_result_flag_deleted_data);
      }
      /* No IRQ callbacks if verify. */
      if (p_fdc->regs[k_intel_fdc_register_internal_command] == 0x1C) {
        do_irqs = 0;
      }
      if (do_irqs) {
        intel_fdc_start_irq_callbacks(p_fdc);
      }
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);

      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_data);
    } else {
      intel_fdc_finish_command(p_fdc, k_intel_fdc_result_clock_error);
    }
    break;
  case k_intel_fdc_state_in_data:
    is_done = intel_fdc_decrement_counter(p_fdc);
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
    if (is_done) {
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
intel_fdc_reset_sector_byte_count(struct intel_fdc_struct* p_fdc) {
  uint8_t msb = p_fdc->regs[k_intel_fdc_register_internal_count_msb_copy];
  p_fdc->regs[k_intel_fdc_register_internal_count_msb] = msb;
  p_fdc->regs[k_intel_fdc_register_internal_count_lsb] = 0x80;
}

static void
intel_fdc_write_FFs_and_00s(struct intel_fdc_struct* p_fdc,
                            int call_context,
                            int num_FFs) {
  if (num_FFs != -1) {
    p_fdc->regs[k_intel_fdc_register_internal_count_lsb] = num_FFs;
    p_fdc->regs[k_intel_fdc_register_internal_count_msb] = 0;
  }
  intel_fdc_do_write_run(p_fdc, call_context, 0xFF);
}

static void
intel_fdc_byte_callback_writing(struct intel_fdc_struct* p_fdc) {
  int is_done;
  uint8_t data;
  uint8_t routine;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;

  assert(p_current_drive != NULL);

  if (intel_fdc_is_irq_callbacks(p_fdc)) {
    if (!intel_fdc_check_data_loss_ok(p_fdc)) {
      return;
    }
    data = p_fdc->regs[k_intel_fdc_register_internal_data];
    p_fdc->mmio_data = data;
    intel_fdc_status_raise(p_fdc, (k_intel_fdc_status_flag_nmi |
                                       k_intel_fdc_status_flag_need_data));
  }

  switch (p_fdc->state) {
  case k_intel_fdc_state_write_run:
    is_done = intel_fdc_decrement_counter(p_fdc);
    if (!is_done) {
      p_fdc->mmio_data =
          p_fdc->regs[k_intel_fdc_register_internal_write_run_data];
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, p_fdc->mmio_data);
      break;
    }
    switch (p_fdc->call_context) {
    case k_intel_fdc_call_write:
      p_fdc->mmio_data = 0x00;
      intel_fdc_start_irq_callbacks(p_fdc);
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_write_data_mark);
      break;
    case k_intel_fdc_call_format_GAP1_or_GAP3_FFs:
      /* Flip from writing 0xFFs to 0x00s. */
      p_fdc->regs[k_intel_fdc_register_internal_count_lsb] = 0x05;
      intel_fdc_do_write_run(p_fdc,
                             k_intel_fdc_call_format_GAP1_or_GAP3_00s,
                             0x00);
      break;
    case k_intel_fdc_call_format_GAP1_or_GAP3_00s:
      p_fdc->mmio_data = 0x00;
      intel_fdc_start_irq_callbacks(p_fdc);
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_write_id_marker);
      break;
    case k_intel_fdc_call_format_GAP2_FFs:
      /* Flip from writing 0xFFs to 0x00s. */
      p_fdc->regs[k_intel_fdc_register_internal_count_lsb] = 0x05;
      intel_fdc_do_write_run(p_fdc,
                             k_intel_fdc_call_format_GAP2_00s,
                             0x00);
      break;
    case k_intel_fdc_call_format_GAP2_00s:
      p_fdc->mmio_data = 0x00;
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_write_data_marker);
      break;
    case k_intel_fdc_call_format_data:
      p_fdc->mmio_data = (p_fdc->crc >> 8);
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_data_crc_2);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_intel_fdc_state_write_data_mark:
    data = p_fdc->regs[k_intel_fdc_register_internal_param_data_marker];
    p_fdc->crc = ibm_disc_format_crc_init();
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
    p_fdc->mmio_data = data;
    p_fdc->mmio_clocks = k_ibm_disc_mark_clock_pattern;
    intel_fdc_reset_sector_byte_count(p_fdc);
    /* This strange decrement is how the ROM does it. */
    p_fdc->regs[k_intel_fdc_register_internal_count_lsb]--;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_write_sector_data);
    break;
  case k_intel_fdc_state_write_sector_data:
    p_fdc->mmio_clocks = 0xFF;
    data = p_fdc->mmio_data;
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
    is_done = intel_fdc_decrement_counter(p_fdc);
    if (is_done) {
      p_fdc->regs[k_intel_fdc_register_internal_dynamic_dispatch] = 0;
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_dynamic_dispatch);
    }
    break;
  case k_intel_fdc_state_write_crc_2:
    p_fdc->mmio_data = (p_fdc->crc & 0xFF);
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_write_crc_3);
    break;
  case k_intel_fdc_state_write_crc_3:
    p_fdc->mmio_data = 0xFF;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_dynamic_dispatch);
    break;
  case k_intel_fdc_state_dynamic_dispatch:
    routine = p_fdc->regs[k_intel_fdc_register_internal_dynamic_dispatch];
    p_fdc->regs[k_intel_fdc_register_internal_dynamic_dispatch]++;
    switch (routine) {
    /* Routines 0 - 2 used for write sector. */
    case 0:
      data = p_fdc->mmio_data;
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
      break;
    case 1:
      p_fdc->mmio_data = (p_fdc->crc >> 8);
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_write_crc_2);
      break;
    case 2:
      intel_fdc_check_completion(p_fdc);
      break;
    /* Routines 4 - 11 used for format. */
    /* 4 - 7 write the 4 user-supplied sector header bytes. */
    case 4:
      p_fdc->mmio_clocks = 0xFF;
      /* Fall through. */
    case 5:
    case 6:
    case 7:
      if (routine == 6) {
        intel_fdc_stop_irq_callbacks(p_fdc);
      }
      data = p_fdc->mmio_data;
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
      break;
    /* 8 write the sector header CRC. */
    case 8:
      p_fdc->mmio_data = (p_fdc->crc >> 8);
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_id_crc_2);
      break;
    /* 9 write GAP2 */
    case 9:
      /* 10 is GAP2 0xFF length minus 1.
       * Minus one because the CRC generator emits a third byte as 0xFF.
       */
      /* -1 here because we will set the count registers ourselves. In the ROM,
       * LSB is written here but not MSB.
       */
      p_fdc->regs[k_intel_fdc_register_internal_count_lsb] = 10;
      intel_fdc_write_FFs_and_00s(p_fdc, k_intel_fdc_call_format_GAP2_FFs, -1);
      break;
    case 10:
      intel_fdc_reset_sector_byte_count(p_fdc);
      intel_fdc_do_write_run(p_fdc, k_intel_fdc_call_format_data, 0xE5);
      break;
    /* 11 is after the sector data CRC is written. */
    case 11:
      p_fdc->mmio_data = 0xFF;
      p_fdc->regs[k_intel_fdc_register_internal_param_3]--;
      if ((p_fdc->regs[k_intel_fdc_register_internal_param_3] & 0x1F) == 0) {
        /* Format sectors done. Write GAP4 until end of track. */
        /* Reset param 3 to 1, to ensure immediate exit in the command exit
         * path in intel_fdc_check_completion().
         */
        p_fdc->regs[k_intel_fdc_register_internal_param_3] = 1;
        p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_stop_format;
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_gap_4);
      } else {
        /* Format sectors not done. Next one. */
        /* Reset state machine index. param 2 is GAP3. */
        p_fdc->regs[k_intel_fdc_register_internal_dynamic_dispatch] = 4;
        intel_fdc_write_FFs_and_00s(
            p_fdc,
            k_intel_fdc_call_format_GAP1_or_GAP3_FFs,
            p_fdc->regs[k_intel_fdc_register_internal_param_2]);
      }
      break;
    default:
      util_bail("dodgy routine");
      break;
    }
    break;
  case k_intel_fdc_state_format_write_id_marker:
    data = k_ibm_disc_id_mark_data_pattern;
    p_fdc->crc = ibm_disc_format_crc_init();
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
    p_fdc->mmio_data = data;
    p_fdc->mmio_clocks = k_ibm_disc_mark_clock_pattern;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_dynamic_dispatch);
    break;
  case k_intel_fdc_state_format_id_crc_2:
    p_fdc->mmio_data = (p_fdc->crc & 0xFF);
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_id_crc_3);
    break;
  case k_intel_fdc_state_format_id_crc_3:
    p_fdc->mmio_data = 0xFF;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_dynamic_dispatch);
    break;
  case k_intel_fdc_state_format_write_data_marker:
    data = k_ibm_disc_data_mark_data_pattern;
    p_fdc->crc = ibm_disc_format_crc_init();
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data);
    p_fdc->mmio_data = data;
    p_fdc->mmio_clocks = k_ibm_disc_mark_clock_pattern;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_dynamic_dispatch);
    break;
  case k_intel_fdc_state_format_data_crc_2:
    p_fdc->mmio_data = (p_fdc->crc & 0xFF);
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_data_crc_3);
    break;
  case k_intel_fdc_state_format_data_crc_3:
    p_fdc->mmio_data = 0xFF;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_dynamic_dispatch);
    break;
  case k_intel_fdc_state_format_gap_4:
    /* GAP 4 writes until the index pulse is hit, which is handled in the index
     * pulse callback.
     */
    p_fdc->mmio_data = 0xFF;
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
  case k_intel_fdc_state_in_data_crc:
  case k_intel_fdc_state_skip_gap_2:
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
  case k_intel_fdc_state_write_run:
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
      intel_fdc_finish_command(p_fdc, k_intel_fdc_result_sector_not_found);
    }
    break;
  case k_intel_fdc_index_pulse_spindown:
    p_fdc->regs[k_intel_fdc_register_internal_index_pulse_count]--;
    if (p_fdc->regs[k_intel_fdc_register_internal_index_pulse_count] == 0) {
      if (p_fdc->log_commands) {
        log_do_log(k_log_disc, k_log_info, "8271: automatic head unload");
      }
      intel_fdc_spindown(p_fdc);
      p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_none;
    }
    break;
  case k_intel_fdc_index_pulse_start_format:
    /* EMU: note that format doesn't set an index pulse timeout. No matter how
     * large the format sector size request, even 16384, the command never
     * exits due to 2 index pulses counted. This differs from read _and_
     * write. Format will exit on the next index pulse after all the sectors
     * have been written.
     * Disc Duplicator III needs this to work correctly when deformatting
     * tracks.
     */
    if (p_fdc->regs[k_intel_fdc_register_internal_param_4] != 0) {
      util_bail("format GAP5 not supported");
    }
    /* Decrement GAP3, because the CRC generator emits a third byte as 0xFF. */
    p_fdc->regs[k_intel_fdc_register_internal_param_2]--;
    p_fdc->regs[k_intel_fdc_register_internal_dynamic_dispatch] = 4;
    /* This will start writing immediately because we check index pulse
     * callbacks before we process read/write state.
     */
    p_fdc->index_pulse_callback = k_intel_fdc_index_pulse_none;
    /* param 5 is GAP1. */
    intel_fdc_write_FFs_and_00s(
        p_fdc,
        k_intel_fdc_call_format_GAP1_or_GAP3_FFs,
        p_fdc->regs[k_intel_fdc_register_internal_param_5]);
    break;
  case k_intel_fdc_index_pulse_stop_format:
    intel_fdc_check_completion(p_fdc);
    break;
  case k_intel_fdc_index_pulse_start_read_id:
    intel_fdc_start_index_pulse_timeout(p_fdc);
    intel_fdc_start_syncing_for_header(p_fdc);
    break;
  default:
    assert(0);
    break;
  }
}

static void
intel_fdc_pulses_callback(void* p, uint32_t pulses, uint32_t count) {
  uint32_t i;
  int bit;
  uint8_t clocks_byte;
  uint8_t data_byte;

  (void) count;
  assert(count == 32);

  struct intel_fdc_struct* p_fdc = (struct intel_fdc_struct*) p;
  struct disc_drive_struct* p_current_drive = p_fdc->p_current_drive;

  ibm_disc_format_2us_pulses_to_fm(&clocks_byte, &data_byte, pulses);

  assert(p_current_drive != NULL);

  intel_fdc_check_index_pulse(p_fdc);

  /* All writing occurs here. */
  /* NOTE: a nice 8271 quirk: if the write gate is open outside a command, it
   * still writes to disc, often effectively creating weak bits.
   */
  if (p_fdc->drive_out & k_intel_fdc_drive_out_write_enable) {
    uint32_t pulses = ibm_disc_format_fm_to_2us_pulses(p_fdc->mmio_clocks,
                                                       p_fdc->mmio_data);
    disc_drive_write_pulses(p_current_drive, pulses);
  }

  switch (p_fdc->state) {
  case k_intel_fdc_state_idle:
    break;
  case k_intel_fdc_state_syncing_for_id_wait:
  case k_intel_fdc_state_syncing_for_id:
  case k_intel_fdc_state_check_id_marker:
  case k_intel_fdc_state_in_id:
  case k_intel_fdc_state_in_id_crc:
  case k_intel_fdc_state_skip_gap_2:
  case k_intel_fdc_state_syncing_for_data:
  case k_intel_fdc_state_check_data_marker:
  case k_intel_fdc_state_in_data:
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
  case k_intel_fdc_state_write_run:
  case k_intel_fdc_state_write_data_mark:
  case k_intel_fdc_state_dynamic_dispatch:
  case k_intel_fdc_state_write_sector_data:
  case k_intel_fdc_state_write_crc_2:
  case k_intel_fdc_state_write_crc_3:
  case k_intel_fdc_state_format_write_id_marker:
  case k_intel_fdc_state_format_id_crc_2:
  case k_intel_fdc_state_format_id_crc_3:
  case k_intel_fdc_state_format_write_data_marker:
  case k_intel_fdc_state_format_data_crc_2:
  case k_intel_fdc_state_format_data_crc_3:
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

  disc_drive_set_pulses_callback(p_drive_0, intel_fdc_pulses_callback, p_fdc);
  disc_drive_set_pulses_callback(p_drive_1, intel_fdc_pulses_callback, p_fdc);
}
