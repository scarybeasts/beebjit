#include "intel_fdc.h"

#include "bbc_options.h"
#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "state_6502.h"
#include "util.h"

#include <assert.h>

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
  k_intel_fdc_max_params = 5,
};

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

enum {
  k_intel_fdc_status_flag_busy = 0x80,
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
  k_intel_fdc_register_scan_sector = 0x06,
  k_intel_fdc_register_head_step_rate = 0x0D,
  k_intel_fdc_register_head_settle_time = 0x0E,
  k_intel_fdc_register_head_load_unload = 0x0F,
  k_intel_fdc_register_bad_track_1_drive_0 = 0x10,
  k_intel_fdc_register_bad_track_2_drive_0 = 0x11,
  k_intel_fdc_register_track_drive_0 = 0x12,
  k_intel_fdc_register_mode = 0x17,
  k_intel_fdc_register_bad_track_1_drive_1 = 0x18,
  k_intel_fdc_register_bad_track_2_drive_1 = 0x19,
  k_intel_fdc_register_track_drive_1 = 0x1A,
  k_intel_fdc_register_drive_out = 0x23,
};

enum {
  k_intel_fdc_drive_out_side = 0x20,
  k_intel_fdc_drive_out_load_head = 0x08,
  k_intel_fdc_drive_out_write_enable = 0x01,
};

enum {
  k_intel_fdc_state_idle = 0,
  k_intel_fdc_state_wait_no_index = 1,
  k_intel_fdc_state_wait_index = 2,
  k_intel_fdc_state_search_id = 3,
  k_intel_fdc_state_in_id = 4,
  k_intel_fdc_state_in_id_crc = 5,
  k_intel_fdc_state_search_data = 6,
  k_intel_fdc_state_in_data = 7,
  k_intel_fdc_state_in_deleted_data = 8,
  k_intel_fdc_state_in_data_crc = 9,
  k_intel_fdc_state_write_gap_2 = 10,
  k_intel_fdc_state_write_sector_data = 11,
  k_intel_fdc_state_format_wait_no_index = 12,
  k_intel_fdc_state_format_wait_index = 13,
  k_intel_fdc_state_format_gap_1 = 14,
  k_intel_fdc_state_format_write_id = 15,
  k_intel_fdc_state_format_write_data = 16,
  k_intel_fdc_state_format_gap_3 = 17,
  k_intel_fdc_state_format_gap_4 = 18,
  k_intel_fdc_state_seeking = 19,
  k_intel_fdc_state_settling = 20,
};

struct intel_fdc_struct {
  struct state_6502* p_state_6502;
  uint32_t timer_id;

  int log_commands;

  struct disc_struct* p_disc_0;
  struct disc_struct* p_disc_1;
  struct disc_struct* p_current_disc;

  uint8_t status;
  uint8_t result;
  uint8_t data;
  uint8_t logical_track[2];
  uint8_t command_pending;
  uint8_t drive_select;
  uint8_t command;
  uint8_t parameters_needed;
  uint8_t parameters_index;
  uint8_t parameters[k_intel_fdc_max_params];
  uint8_t drive_out;
  uint8_t register_mode;
  uint8_t register_head_step_rate;
  uint8_t register_head_settle_time;
  uint8_t register_head_load_unload;

  uint8_t command_track;
  uint8_t command_sector;
  uint8_t command_num_sectors;
  uint32_t command_sector_size;
  int command_is_transfer_deleted;
  int command_is_verify_only;
  int command_is_write;

  uint8_t current_sector;
  uint8_t current_sectors_left;
  int current_had_deleted_data;
  int current_needs_settle;
  uint8_t current_format_gap1;
  uint8_t current_format_gap3;
  uint8_t current_format_gap5;
  int32_t current_head_unload_count;
  uint32_t current_seek_count;

  int state;
  uint32_t state_count;
  int state_is_index_pulse;
  uint32_t state_index_pulse_count;
  uint8_t state_id_track;
  uint8_t state_id_sector;
  uint16_t crc;
  uint16_t on_disc_crc;
};

static void
intel_fdc_set_drive_out(struct intel_fdc_struct* p_fdc, uint8_t drive_out) {
  struct disc_struct* p_current_disc = p_fdc->p_current_disc;

  if (p_current_disc != NULL) {
    if ((p_fdc->drive_out & k_intel_fdc_drive_out_load_head) !=
        (drive_out & k_intel_fdc_drive_out_load_head)) {
      if (drive_out & k_intel_fdc_drive_out_load_head) {
        disc_start_spinning(p_current_disc);
      } else {
        disc_stop_spinning(p_current_disc);
      }
    }
    disc_select_side(p_current_disc,
                     !!(drive_out & k_intel_fdc_drive_out_side));
  }

  p_fdc->drive_out = drive_out;
}

static void
intel_fdc_select_drive(struct intel_fdc_struct* p_fdc,
                       uint8_t new_drive_select) {
  uint8_t new_drive_out;

  if (new_drive_select == p_fdc->drive_select) {
    return;
  }

  /* A change of drive select bits clears various bits in the drive output.
   * For example, the newly selected drive won't have the load head signal
   * active.
   * This also spins down the previously selected drive.
   */
  new_drive_out = (p_fdc->drive_out & ~0xC0);
  new_drive_out |= new_drive_select;
  new_drive_out &= ~k_intel_fdc_drive_out_load_head;
  new_drive_out &= ~k_intel_fdc_drive_out_write_enable;
  intel_fdc_set_drive_out(p_fdc, new_drive_out);

  p_fdc->drive_select = new_drive_select;
  if (new_drive_select == 0x40) {
    p_fdc->p_current_disc = p_fdc->p_disc_0;
  } else if (new_drive_select == 0x80) {
    p_fdc->p_current_disc = p_fdc->p_disc_1;
  } else {
    /* NOTE: I'm not sure what selecting no drive or selecting both drives is
     * supposed to do.
     */
    p_fdc->p_current_disc = NULL;
  }
}

static void
intel_fdc_set_state(struct intel_fdc_struct* p_fdc, int state) {
  p_fdc->state = state;
  p_fdc->state_count = 0;

  if (p_fdc->state == k_intel_fdc_state_idle) {
    uint8_t head_unload_count = (p_fdc->register_head_load_unload >> 4);

    p_fdc->state_index_pulse_count = 0;
    if (head_unload_count == 0) {
      /* Unload immediately. */
      intel_fdc_select_drive(p_fdc, 0);
    } else if (head_unload_count == 0xF) {
      /* Never automatically unload. */
      p_fdc->current_head_unload_count = -1;
    } else {
      p_fdc->current_head_unload_count = head_unload_count;
    }
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
    disc_write_byte(p_fdc->p_current_disc, 0xFF, 0xFF);
  }

  /* Lower any NMI assertion. This is particularly important for error $0A,
   * aka. late DMA, which will abort the command while NMI is asserted. We
   * therefore need to de-assert NMI so that the NMI for command completion
   * isn't lost.
   */
  state_6502_set_irq_level(p_fdc->p_state_6502, k_state_6502_irq_nmi, 0);
}

static void
intel_fdc_do_reset(struct intel_fdc_struct* p_fdc) {
  if (p_fdc->log_commands) {
    log_do_log(k_log_disc, k_log_info, "8271: reset");
  }

  /* Abort any in-progress command. */
  intel_fdc_command_abort(p_fdc);
  intel_fdc_set_state(p_fdc, k_intel_fdc_state_idle);
  p_fdc->command = 0;

  /* Deselect any drive; ensures spin-down. */
  intel_fdc_select_drive(p_fdc, 0);
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
intel_fdc_set_drives(struct intel_fdc_struct* p_fdc,
                     struct disc_struct* p_disc_0,
                     struct disc_struct* p_disc_1) {
  assert(p_fdc->p_disc_0 == NULL);
  assert(p_fdc->p_disc_1 == NULL);
  p_fdc->p_disc_0 = p_disc_0;
  p_fdc->p_disc_1 = p_disc_1;

  disc_set_byte_callback(p_disc_0, intel_fdc_byte_callback, p_fdc);
  disc_set_byte_callback(p_disc_1, intel_fdc_byte_callback, p_fdc);
}

void
intel_fdc_destroy(struct intel_fdc_struct* p_fdc) {
  struct disc_struct* p_disc_0 = p_fdc->p_disc_0;
  struct disc_struct* p_disc_1 = p_fdc->p_disc_1;

  disc_set_byte_callback(p_disc_0, NULL, NULL);
  disc_set_byte_callback(p_disc_1, NULL, NULL);

  if (disc_is_spinning(p_disc_0)) {
    disc_stop_spinning(p_disc_0);
  }
  if (disc_is_spinning(p_disc_1)) {
    disc_stop_spinning(p_disc_1);
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

  p_fdc->status = status;
  p_fdc->result = result;

  if (firing && (level == 1)) {
    log_do_log(k_log_disc, k_log_error, "edge triggered NMI already high");
  }

  state_6502_set_irq_level(p_fdc->p_state_6502,
                           k_state_6502_irq_nmi,
                           level);
}

static void
intel_fdc_set_command_result(struct intel_fdc_struct* p_fdc,
                             int do_nmi,
                             uint8_t result) {
  uint8_t status = k_intel_fdc_status_flag_result_ready;
  if (p_fdc->current_had_deleted_data) {
    result |= k_intel_fdc_result_flag_deleted_data;
  }
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
  p_fdc->command = 0;
}

uint8_t
intel_fdc_read(struct intel_fdc_struct* p_fdc, uint16_t addr) {
  switch (addr & 0x07) {
  case k_intel_fdc_status:
    return p_fdc->status;
  case k_intel_fdc_result:
    intel_fdc_set_status_result(p_fdc,
                                (p_fdc->status &
                                 ~(k_intel_fdc_status_flag_result_ready |
                                   k_intel_fdc_status_flag_nmi)),
                                p_fdc->result);
    return p_fdc->result;
  /* EMU: on a real model B, the i8271 has the data register mapped for all of
   * register address 4 - 7.
   */
  case k_intel_fdc_data:
  case (k_intel_fdc_data + 1):
  case (k_intel_fdc_data + 2):
  case (k_intel_fdc_data + 3):
    intel_fdc_set_status_result(p_fdc,
                                (p_fdc->status &
                                 ~(k_intel_fdc_status_flag_need_data |
                                   k_intel_fdc_status_flag_nmi)),
                                p_fdc->result);
    return p_fdc->data;
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
  struct disc_struct* p_current_disc = p_fdc->p_current_disc;
  if (p_current_disc == NULL) {
    return 0;
  }
  return disc_is_write_protected(p_current_disc);
}

static int
intel_fdc_get_TRK0(struct intel_fdc_struct* p_fdc) {
  struct disc_struct* p_current_disc = p_fdc->p_current_disc;
  if (p_current_disc == NULL) {
    return 0;
  }
  return (disc_get_track(p_current_disc) == 0);
}

static int
intel_fdc_get_INDEX(struct intel_fdc_struct* p_fdc) {
  struct disc_struct* p_current_disc = p_fdc->p_current_disc;
  if (p_current_disc == NULL) {
    return 0;
  }
  return disc_is_index_pulse(p_current_disc);
}

static int
intel_fdc_current_disc_is_spinning(struct intel_fdc_struct* p_fdc) {
  struct disc_struct* p_current_disc = p_fdc->p_current_disc;
  if (p_current_disc == NULL) {
    return 0;
  }
  return disc_is_spinning(p_current_disc);
}

static int
intel_fdc_do_seek_step(struct intel_fdc_struct* p_fdc) {
  int32_t delta;
  uint8_t* p_logical_track;

  struct disc_struct* p_current_disc = p_fdc->p_current_disc;
  assert(p_current_disc != NULL);

  if (p_current_disc == p_fdc->p_disc_0) {
    p_logical_track = &p_fdc->logical_track[0];
  } else {
    p_logical_track = &p_fdc->logical_track[1];
  }

  /* A seek is generally a number of head steps relative to the logical track
   * register, which may not match the physical head position.
   * An important exception is a seek to track 0, which head steps until the
   * TRK0 signal is detected.
   * EMU: the special TRK0 logic for track 0 applies to both an explicit seek
   * command as well as the implicit seek present in many non-seek commands.
   */
  if (p_fdc->command_track == 0) {
    if (disc_get_track(p_current_disc) == 0) {
      *p_logical_track = 0;
      return 0;
    }
    disc_seek_track(p_current_disc, -1);
    return 1;
  }

  if (p_fdc->command_track == *p_logical_track) {
    return 0;
  }

  /* EMU TODO: I was seeing weirdness on real hardware with large logical
   * track numbers (0xFx). Signedness issue or bad track register issue?
   */
  if (p_fdc->command_track > *p_logical_track) {
    delta = 1;
  } else {
    delta = -1;
  }

  disc_seek_track(p_current_disc, delta);
  *p_logical_track += delta;

  return 1;
}

static void
intel_fdc_write_special_register(struct intel_fdc_struct* p_fdc,
                                 uint8_t reg,
                                 uint8_t val) {
  switch (reg) {
  case k_intel_fdc_register_head_step_rate:
    p_fdc->register_head_step_rate = val;
    break;
  case k_intel_fdc_register_head_settle_time:
    p_fdc->register_head_settle_time = val;
    break;
  case k_intel_fdc_register_head_load_unload:
    p_fdc->register_head_load_unload = val;
    break;
  case k_intel_fdc_register_track_drive_0:
    p_fdc->logical_track[0] = val;
    break;
  case k_intel_fdc_register_track_drive_1:
    p_fdc->logical_track[1] = val;
    break;
  case k_intel_fdc_register_mode:
    p_fdc->register_mode = (val & 0x07);
    break;
  case k_intel_fdc_register_bad_track_1_drive_0:
  case k_intel_fdc_register_bad_track_2_drive_0:
  case k_intel_fdc_register_bad_track_1_drive_1:
  case k_intel_fdc_register_bad_track_2_drive_1:
    break;
  case k_intel_fdc_register_drive_out:
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
    assert(0);
    break;
  }
}

static void
intel_fdc_do_command(struct intel_fdc_struct* p_fdc) {
  uint8_t temp_u8;
  uint8_t command;

  uint8_t param0 = p_fdc->parameters[0];
  uint8_t param1 = p_fdc->parameters[1];
  uint8_t param2 = p_fdc->parameters[2];
  uint8_t param3 = p_fdc->parameters[3];
  uint8_t param4 = p_fdc->parameters[4];

  assert(p_fdc->state == k_intel_fdc_state_idle);
  assert(p_fdc->state_count == 0);
  assert(p_fdc->command == 0);
  assert(p_fdc->parameters_needed == 0);

  command = (p_fdc->command_pending & 0x3F);
  p_fdc->command = command;

  /* For the single 128 byte sector commands, fake the parameters. */
  switch (command) {
  case k_intel_fdc_command_write_sector_128:
  case k_intel_fdc_command_write_sector_deleted_128:
  case k_intel_fdc_command_read_sector_128:
  case k_intel_fdc_command_read_sector_with_deleted_128:
  case k_intel_fdc_command_verify_sector_128:
    param2 = 1;
  default:
    break;
  }

  p_fdc->command_track = param0;
  p_fdc->command_sector = param1;
  /* EMU: this is correct even for read sector IDs ($1B), even though the
   * data sheet suggests all the bits are relevant.
   */
  p_fdc->command_num_sectors = (param2 & 0x1F);
  p_fdc->command_sector_size = (128 << (param2 >> 5));
  p_fdc->command_is_transfer_deleted = 0;
  p_fdc->command_is_verify_only = 0;
  p_fdc->command_is_write = 0;

  intel_fdc_select_drive(p_fdc, (p_fdc->command_pending & 0xC0));

  if (p_fdc->log_commands) {
    int32_t head_pos = -1;
    int32_t track = -1;
    struct disc_struct* p_current_disc = p_fdc->p_current_disc;

    if (p_current_disc != NULL) {
      head_pos = (int32_t) disc_get_head_position(p_current_disc);
      track = (int32_t) disc_get_track(p_current_disc);
    }
    log_do_log(k_log_disc,
               k_log_info,
               "8271: command $%x sel $%x params $%x $%x $%x $%x $%x "
               "ptrk %d hpos %d",
               command,
               p_fdc->drive_select,
               param0,
               param1,
               param2,
               param3,
               param4,
               track,
               head_pos);
  }

  /* Many commands ensure the head is loaded. The same set of commands also
   * perform an implicit seek.
   */
  switch (command) {
  case k_intel_fdc_command_write_sector_128:
  case k_intel_fdc_command_write_sectors:
  case k_intel_fdc_command_write_sector_deleted_128:
  case k_intel_fdc_command_write_sectors_deleted:
  case k_intel_fdc_command_read_sector_128:
  case k_intel_fdc_command_read_sectors:
  case k_intel_fdc_command_read_sector_with_deleted_128:
  case k_intel_fdc_command_read_sectors_with_deleted:
  case k_intel_fdc_command_read_sector_ids:
  case k_intel_fdc_command_verify_sector_128:
  case k_intel_fdc_command_verify_sectors:
  case k_intel_fdc_command_format:
  case k_intel_fdc_command_seek:
    temp_u8 = p_fdc->drive_out;
    temp_u8 &= ~k_intel_fdc_drive_out_write_enable;
    temp_u8 |= k_intel_fdc_drive_out_load_head;
    intel_fdc_set_drive_out(p_fdc, temp_u8);
  default:
    break;
  }

  /* The write commands bail if the disc is write protected. */
  switch (command) {
  case k_intel_fdc_command_write_sector_128:
  case k_intel_fdc_command_write_sectors:
  case k_intel_fdc_command_write_sector_deleted_128:
  case k_intel_fdc_command_write_sectors_deleted:
  case k_intel_fdc_command_format:
    if (intel_fdc_get_WRPROT(p_fdc)) {
      intel_fdc_set_command_result(p_fdc,
                                   1,
                                   k_intel_fdc_result_write_protected);
      return;
    }
    p_fdc->command_is_write = 1;
    break;
  default:
    break;
  }

  switch (command) {
  case k_intel_fdc_command_write_sector_deleted_128:
  case k_intel_fdc_command_write_sectors_deleted:
  case k_intel_fdc_command_read_sector_with_deleted_128:
  case k_intel_fdc_command_read_sectors_with_deleted:
    p_fdc->command_is_transfer_deleted = 1;
    break;
  case k_intel_fdc_command_verify_sector_128:
  case k_intel_fdc_command_verify_sectors:
    p_fdc->command_is_verify_only = 1;
    break;
  default:
    break;
  }

  p_fdc->state_index_pulse_count = 0;
  p_fdc->current_had_deleted_data = 0;
  p_fdc->current_needs_settle = 0;
  p_fdc->current_seek_count = 0;

  switch (command) {
  case k_intel_fdc_command_write_sector_128:
  case k_intel_fdc_command_write_sectors:
  case k_intel_fdc_command_write_sector_deleted_128:
  case k_intel_fdc_command_write_sectors_deleted:
  case k_intel_fdc_command_read_sector_128:
  case k_intel_fdc_command_read_sectors:
  case k_intel_fdc_command_read_sector_with_deleted_128:
  case k_intel_fdc_command_read_sectors_with_deleted:
  case k_intel_fdc_command_verify_sector_128:
  case k_intel_fdc_command_verify_sectors:
  case k_intel_fdc_command_read_sector_ids:
  case k_intel_fdc_command_format:
  case k_intel_fdc_command_seek:
    /* Not all of these variable are used by all of the commands but set them
     * all for simplicity.
     */
    p_fdc->current_sector = p_fdc->command_sector;
    p_fdc->current_sectors_left = p_fdc->command_num_sectors;
    p_fdc->current_format_gap1 = param4;
    p_fdc->current_format_gap3 = param1;
    p_fdc->current_format_gap5 = param3;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_seeking);
    break;
  case k_intel_fdc_command_read_drive_status:
    temp_u8 = 0x80;
    if (!intel_fdc_current_disc_is_spinning(p_fdc)) {
      intel_fdc_set_command_result(p_fdc, 0, temp_u8);
      break;
    }
    if (intel_fdc_get_TRK0(p_fdc)) {
      /* TRK0 */
      temp_u8 |= 0x02;
    }
    if (p_fdc->drive_select & 0x40) {
      /* RDY0 */
      temp_u8 |= 0x04;
    }
    if (p_fdc->drive_select & 0x80) {
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
  case k_intel_fdc_command_specify:
    intel_fdc_write_special_register(p_fdc, param0, param1);
    intel_fdc_write_special_register(p_fdc, (param0 + 1), param2);
    intel_fdc_write_special_register(p_fdc, (param0 + 2), param3);
    /* EMU: return value matches real 8271. */
    intel_fdc_set_command_result(p_fdc, 0, k_intel_fdc_result_ok);
    break;
  case k_intel_fdc_command_read_special_register:
    temp_u8 = 0;
    switch (param0) {
    case k_intel_fdc_register_scan_sector:
      /* DFS-0.9 reads this register after an error. It is used to report the
       * sector in error.
       */
      temp_u8 = p_fdc->current_sector;
      break;
    case k_intel_fdc_register_head_step_rate:
      temp_u8 = p_fdc->register_head_step_rate;
      break;
    case k_intel_fdc_register_head_settle_time:
      temp_u8 = p_fdc->register_head_settle_time;
      break;
    case k_intel_fdc_register_head_load_unload:
      temp_u8 = p_fdc->register_head_load_unload;
      break;
    case k_intel_fdc_register_mode:
      /* Phantom Combat (BBC B 32K version) reads this?! */
      temp_u8 = (0xC0 | p_fdc->register_mode);
      break;
    case k_intel_fdc_register_drive_out:
      /* DFS-1.2 reads drive out in normal operation. */
      temp_u8 = p_fdc->drive_out;
      break;
    default:
      assert(0);
      break;
    }
    intel_fdc_set_command_result(p_fdc, 0, temp_u8);
    break;
  case k_intel_fdc_command_write_special_register:
    intel_fdc_write_special_register(p_fdc, param0, param1);
    /* EMU: checked on a real 8271.  */
    intel_fdc_set_command_result(p_fdc, 0, k_intel_fdc_result_ok);
    break;
  default:
    assert(0);
  }
}

void
intel_fdc_write(struct intel_fdc_struct* p_fdc,
                uint16_t addr,
                uint8_t val) {
  uint8_t num_params;

  switch (addr & 0x07) {
  case k_intel_fdc_command:
    /* TODO: this isn't correct. A new command will actually replace the old
     * one despite it being decried as illegal.
     */
    if (p_fdc->status & k_intel_fdc_status_flag_busy) {
      /* Need parameters or command busy. Get out. */
      return;
    }

    assert(p_fdc->state == k_intel_fdc_state_idle);
    assert(p_fdc->state_count == 0);
    assert(p_fdc->command == 0);

    p_fdc->command_pending = val;

    switch (val & 0x3F) {
    case k_intel_fdc_command_read_drive_status:
      num_params = 0;
      break;
    case k_intel_fdc_command_seek:
    case k_intel_fdc_command_read_special_register:
      num_params = 1;
      break;
    case k_intel_fdc_command_write_sector_128:
    case k_intel_fdc_command_write_sector_deleted_128:
    case k_intel_fdc_command_read_sector_128:
    case k_intel_fdc_command_read_sector_with_deleted_128:
    case k_intel_fdc_command_verify_sector_128:
    case k_intel_fdc_command_write_special_register:
      num_params = 2;
      break;
    case k_intel_fdc_command_write_sectors:
    case k_intel_fdc_command_read_sectors:
    case k_intel_fdc_command_read_sectors_with_deleted:
    case k_intel_fdc_command_read_sector_ids:
    case k_intel_fdc_command_verify_sectors:
      num_params = 3;
      break;
    case k_intel_fdc_command_specify:
      num_params = 4;
      break;
    case k_intel_fdc_command_format:
      num_params = 5;
      break;
    case k_intel_fdc_command_scan_sectors_with_deleted:
    case k_intel_fdc_command_scan_sectors:
      util_bail("unimplemented 8271 command %x", (val & 0x3F));
    default:
      /* TODO: this isn't right. All the command IDs seem to do something,
       * usually a different-parameter version of another command.
       */
      intel_fdc_set_command_result(p_fdc,
                                   1,
                                   k_intel_fdc_result_sector_not_found);
      return;
    }

    p_fdc->parameters_needed = num_params;
    p_fdc->parameters_index = 0;

    if (p_fdc->parameters_needed == 0) {
      intel_fdc_do_command(p_fdc);
    } else {
      /* TODO: check this, sometimes I see result ready indicated on a real
       * 8271??
       */
      intel_fdc_set_status_result(p_fdc,
                                  k_intel_fdc_status_flag_busy,
                                  k_intel_fdc_result_ok);
    }
    break;
  case k_intel_fdc_parameter:
    if (p_fdc->parameters_needed == 0) {
      break;
    }

    assert(p_fdc->status & k_intel_fdc_status_flag_busy);
    assert(p_fdc->state == k_intel_fdc_state_idle);
    assert(p_fdc->state_count == 0);
    assert(p_fdc->command == 0);

    p_fdc->parameters[p_fdc->parameters_index] = val;
    p_fdc->parameters_index++;
    p_fdc->parameters_needed--;

    if (p_fdc->parameters_needed == 0) {
      intel_fdc_do_command(p_fdc);
    }
    break;
  case k_intel_fdc_data:
  case (k_intel_fdc_data + 1):
  case (k_intel_fdc_data + 2):
  case (k_intel_fdc_data + 3):
    intel_fdc_set_status_result(p_fdc,
                                (p_fdc->status &
                                 ~(k_intel_fdc_status_flag_need_data |
                                   k_intel_fdc_status_flag_nmi)),
                                p_fdc->result);
    p_fdc->data = val;
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
      intel_fdc_do_reset(p_fdc);
    }
    break;
  default:
    assert(0);
    break;
  }
}

static int
intel_fdc_check_data_loss_ok(struct intel_fdc_struct* p_fdc) {
  if (p_fdc->status & k_intel_fdc_status_flag_need_data) {
    intel_fdc_command_abort(p_fdc);
    intel_fdc_set_command_result(p_fdc, 1, k_intel_fdc_result_late_dma);
    return 0;
  }
  return 1;
}

static int
intel_fdc_provide_data_byte(struct intel_fdc_struct* p_fdc, uint8_t byte) {
  if (!intel_fdc_check_data_loss_ok(p_fdc)) {
    return 0;
  }
  p_fdc->data = byte;
  intel_fdc_set_status_result(p_fdc,
                              (k_intel_fdc_status_flag_busy |
                                  k_intel_fdc_status_flag_nmi |
                                  k_intel_fdc_status_flag_need_data),
                                  k_intel_fdc_result_ok);
  return 1;
}

static int
intel_fdc_consume_data_byte(struct intel_fdc_struct* p_fdc) {
  if (!intel_fdc_check_data_loss_ok(p_fdc)) {
    return 0;
  }

  disc_write_byte(p_fdc->p_current_disc, p_fdc->data, 0xFF);
  return 1;
}

static int
intel_fdc_check_crc(struct intel_fdc_struct* p_fdc, uint8_t error) {
  if (p_fdc->crc == p_fdc->on_disc_crc) {
    return 1;
  }

  intel_fdc_set_command_result(p_fdc, 1, error);
  return 0;
}

static void
intel_fdc_check_completion(struct intel_fdc_struct* p_fdc) {
  p_fdc->current_sectors_left--;
  /* Specifying 0 sectors seems to result in 32 read, presumably due to
   * underflow of the 5-bit counter.
   */
  p_fdc->current_sectors_left &= 0x1F;
  if (p_fdc->current_sectors_left == 0) {
    intel_fdc_set_command_result(p_fdc, 1, k_intel_fdc_result_ok);
  } else {
    p_fdc->current_sector++;
    /* EMU: Reset the index pulse counter.
     * A real 8271 permits two index pulses per sector, not per command.
     */
    p_fdc->state_index_pulse_count = 0;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_search_id);
  }
}

void
intel_fdc_byte_callback(void* p, uint8_t data_byte, uint8_t clocks_byte) {
  int was_index_pulse;
  int did_step;

  struct intel_fdc_struct* p_fdc = (struct intel_fdc_struct*) p;
  struct disc_struct* p_current_disc = p_fdc->p_current_disc;

  assert(p_current_disc != NULL);

  was_index_pulse =  p_fdc->state_is_index_pulse;
  p_fdc->state_is_index_pulse = intel_fdc_get_INDEX(p_fdc);

  switch (p_fdc->state) {
  case k_intel_fdc_state_idle:
    /* If the write gate is open outside a command, it cleans flux transitions
     * from the disc surface, effectively creating weak bits!
     */
    if ((p_fdc->drive_out & k_intel_fdc_drive_out_write_enable) &&
        !disc_is_write_protected(p_current_disc)) {
      disc_write_byte(p_current_disc, 0x00, 0x00);
    }
    break;
  case k_intel_fdc_state_seeking:
    /* Seeking doesn't time out due to index pulse counting. */
    p_fdc->state_index_pulse_count = 0;

    if (p_fdc->current_seek_count) {
      p_fdc->current_seek_count--;
      break;
    }

    did_step = intel_fdc_do_seek_step(p_fdc);
    p_fdc->current_needs_settle |= did_step;
    if (did_step) {
      /* EMU NOTE: the datasheet is ambiguous about whether the units are 1ms
       * or 2ms for 5.25" drives. 1ms might be your best guess from the
       * datasheet, but timing on a real machine, it appears to be 2ms.
       */
      p_fdc->current_seek_count = (p_fdc->register_head_step_rate * 1000);
      p_fdc->current_seek_count *= 2;
      /* Calculate how many 64us chunks for the head step time. */
      p_fdc->current_seek_count /= 64;
      break;
    }
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_settling);
    break;
  case k_intel_fdc_state_settling:
    /* Settling doesn't time out due to index pulse counting. */
    p_fdc->state_index_pulse_count = 0;

    if (p_fdc->current_seek_count) {
      p_fdc->current_seek_count--;
      break;
    }

    if (p_fdc->current_needs_settle) {
      p_fdc->current_needs_settle = 0;
      /* EMU: all references state the units are 2ms for 5.25" drives. */
      p_fdc->current_seek_count = (p_fdc->register_head_settle_time * 1000);
      p_fdc->current_seek_count *= 2;
      /* Calculate how many 64us chunks for the head step time. */
      p_fdc->current_seek_count /= 64;
      break;
    }

    switch (p_fdc->command) {
    case k_intel_fdc_command_read_sector_ids:
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_wait_no_index);
      break;
    case k_intel_fdc_command_format:
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_wait_no_index);
      break;
    case k_intel_fdc_command_seek:
      intel_fdc_set_command_result(p_fdc, 1, k_intel_fdc_result_ok);
      break;
    default:
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_search_id);
      break;
    }

    break;
  case k_intel_fdc_state_wait_no_index:
    if (!p_fdc->state_is_index_pulse) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_wait_index);
    }
    break;
  case k_intel_fdc_state_wait_index:
    if (p_fdc->state_is_index_pulse) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_search_id);
    }
    break;
  case k_intel_fdc_state_search_id:
    if ((clocks_byte == k_ibm_disc_mark_clock_pattern) &&
        (data_byte == k_ibm_disc_id_mark_data_pattern)) {
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc =
          ibm_disc_format_crc_add_byte(p_fdc->crc,
                                       k_ibm_disc_id_mark_data_pattern);

      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_id);
    }
    break;
  case k_intel_fdc_state_in_id:
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
    if (p_fdc->command == k_intel_fdc_command_read_sector_ids) {
      if (!intel_fdc_provide_data_byte(p_fdc, data_byte)) {
        break;
      }
    }
    switch (p_fdc->state_count) {
    case 0:
      p_fdc->state_id_track = data_byte;
      break;
    case 2:
      p_fdc->state_id_sector = data_byte;
      break;
    default:
      break;
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == 4) {
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
      if (p_fdc->command == k_intel_fdc_command_read_sector_ids) {
        intel_fdc_check_completion(p_fdc);
      } else if (p_fdc->state_id_track != p_fdc->command_track) {
        /* EMU TODO: upon any mismatch of found track vs. expected track,
         * the drive will try twice more on the next two tracks.
         */
        intel_fdc_set_command_result(p_fdc,
                                     1,
                                     k_intel_fdc_result_sector_not_found);
      } else if (p_fdc->state_id_sector == p_fdc->current_sector) {
        p_fdc->state_index_pulse_count = 0;
        if (p_fdc->command_is_write) {
          intel_fdc_set_state(p_fdc, k_intel_fdc_state_write_gap_2);
        } else {
          intel_fdc_set_state(p_fdc, k_intel_fdc_state_search_data);
        }
      } else {
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_search_id);
      }
    }
    break;
  case k_intel_fdc_state_search_data:
    /* EMU TODO: implement these results from a real 8271: if another sector ID
     * header is hit instead of the data mark, that's $10. Also, there are
     * strict requirements on the sync bytes in between header and data. At
     * least 14 bytes are needed post-header and at least the last 2 of these
     * must be 0x00. Violations => $10.
     */
    if ((clocks_byte == k_ibm_disc_mark_clock_pattern) &&
        ((data_byte == k_ibm_disc_data_mark_data_pattern) ||
            (data_byte == k_ibm_disc_deleted_data_mark_data_pattern))) {
      int new_state = k_intel_fdc_state_in_data;
      if (data_byte == k_ibm_disc_deleted_data_mark_data_pattern) {
        p_fdc->current_had_deleted_data = 1;
        new_state = k_intel_fdc_state_in_deleted_data;
      }
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);

      intel_fdc_set_state(p_fdc, new_state);
    }
    break;
  case k_intel_fdc_state_in_data:
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
    if (!p_fdc->command_is_verify_only) {
      if (!intel_fdc_provide_data_byte(p_fdc, data_byte)) {
        break;
      }
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == p_fdc->command_sector_size) {
      p_fdc->on_disc_crc = 0;
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_data_crc);
    }
    break;
  case k_intel_fdc_state_in_deleted_data:
    p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, data_byte);
    if (!p_fdc->command_is_verify_only && p_fdc->command_is_transfer_deleted) {
      if (!intel_fdc_provide_data_byte(p_fdc, data_byte)) {
        break;
      }
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == p_fdc->command_sector_size) {
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
  case k_intel_fdc_state_write_gap_2:
    if (p_fdc->state_count < 11) {
      disc_write_byte(p_current_disc, 0xFF, 0xFF);
    } else {
      disc_write_byte(p_current_disc, 0x00, 0xFF);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == (11 + 6)) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_write_sector_data);
    }
    break;
  case k_intel_fdc_state_write_sector_data:
    if (p_fdc->state_count == 0) {
      uint8_t mark_byte;
      if (p_fdc->command_is_transfer_deleted) {
        mark_byte = k_ibm_disc_deleted_data_mark_data_pattern;
      } else {
        mark_byte = k_ibm_disc_data_mark_data_pattern;
      }
      disc_write_byte(p_current_disc, mark_byte, k_ibm_disc_mark_clock_pattern);
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, mark_byte);
    } else if (p_fdc->state_count < (p_fdc->command_sector_size + 1)) {
      if (!intel_fdc_consume_data_byte(p_fdc)) {
        break;
      }
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, p_fdc->data);
    } else if (p_fdc->state_count == (p_fdc->command_sector_size + 1)) {
      disc_write_byte(p_current_disc, (p_fdc->crc >> 8), 0xFF);
    } else if (p_fdc->state_count == (p_fdc->command_sector_size + 2)) {
      disc_write_byte(p_current_disc, (p_fdc->crc & 0xFF), 0xFF);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == (p_fdc->command_sector_size + 3)) {
      intel_fdc_check_completion(p_fdc);
    } else if (p_fdc->state_count < (p_fdc->command_sector_size + 1)) {
      intel_fdc_set_status_result(p_fdc,
                                  (k_intel_fdc_status_flag_busy |
                                      k_intel_fdc_status_flag_nmi |
                                      k_intel_fdc_status_flag_need_data),
                                      k_intel_fdc_result_ok);
    }
    break;
  case k_intel_fdc_state_format_wait_no_index:
    if (!p_fdc->state_is_index_pulse) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_wait_index);
    }
    break;
  case k_intel_fdc_state_format_wait_index:
    if (!p_fdc->state_is_index_pulse) {
      break;
    }
    assert(p_fdc->current_format_gap5 == 0);
    p_fdc->state_index_pulse_count = 0;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_gap_1);
    /* FALL THROUGH -- need to start writing immediately. */
  case k_intel_fdc_state_format_gap_1:
    if (p_fdc->state_count < p_fdc->current_format_gap1) {
      disc_write_byte(p_current_disc, 0xFF, 0xFF);
    } else {
      disc_write_byte(p_current_disc, 0x00, 0xFF);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == (uint32_t) (p_fdc->current_format_gap1 + 6)) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_write_id);
    }
    break;
  case k_intel_fdc_state_format_write_id:
    if (p_fdc->state_count == 0) {
      disc_write_byte(p_current_disc,
                      k_ibm_disc_id_mark_data_pattern,
                      k_ibm_disc_mark_clock_pattern);
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc =
          ibm_disc_format_crc_add_byte(p_fdc->crc,
                                       k_ibm_disc_id_mark_data_pattern);
    } else if (p_fdc->state_count < 5) {
      if (!intel_fdc_consume_data_byte(p_fdc)) {
        break;
      }
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, p_fdc->data);
    } else if (p_fdc->state_count == 5) {
      disc_write_byte(p_current_disc, (p_fdc->crc >> 8), 0xFF);
    } else if (p_fdc->state_count == 6) {
      disc_write_byte(p_current_disc, (p_fdc->crc & 0xFF), 0xFF);
    } else if (p_fdc->state_count < 18) {
      /* GAP 2 $FF's x11 */
      disc_write_byte(p_current_disc, 0xFF, 0xFF);
    } else {
      /* GAP 2 $00's x6 */
      disc_write_byte(p_current_disc, 0x00, 0xFF);
    }

    p_fdc->state_count++;
    if (p_fdc->state_count < 5) {
      intel_fdc_set_status_result(p_fdc,
                                  (k_intel_fdc_status_flag_busy |
                                      k_intel_fdc_status_flag_nmi |
                                      k_intel_fdc_status_flag_need_data),
                                      k_intel_fdc_result_ok);
    } else if (p_fdc->state_count == (7 + 11 + 6)) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_write_data);
    }
    break;
  case k_intel_fdc_state_format_write_data:
    /* EMU: no matter how large the format sector size request, even 16384, the
     * command never exits due to 2 index pulses counted. This differs from read
     * _and_ write.
     * Disc Duplicator III needs this to work correctly when deformatting
     * tracks.
     */
    p_fdc->state_index_pulse_count = 0;

    if (p_fdc->state_count == 0) {
      uint8_t byte = k_ibm_disc_data_mark_data_pattern;
      disc_write_byte(p_current_disc, byte, k_ibm_disc_mark_clock_pattern);
      p_fdc->crc = ibm_disc_format_crc_init();
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, byte);
    } else if (p_fdc->state_count < (p_fdc->command_sector_size + 1)) {
      uint8_t byte = 0xE5;
      disc_write_byte(p_current_disc, byte, 0xFF);
      p_fdc->crc = ibm_disc_format_crc_add_byte(p_fdc->crc, byte);
    } else if (p_fdc->state_count == (p_fdc->command_sector_size + 1)) {
      /* Formatted sector data is constant so we can check our CRC algorithm
       * here with this assert.
       */
      if (p_fdc->command_sector_size == 256) {
        assert(p_fdc->crc == 0xA40C);
      }
      disc_write_byte(p_current_disc, (p_fdc->crc >> 8), 0xFF);
    } else {
      disc_write_byte(p_current_disc, (p_fdc->crc & 0xFF), 0xFF);
    }

    p_fdc->state_count++;
    if (p_fdc->state_count == (p_fdc->command_sector_size + 3)) {
      p_fdc->current_sectors_left--;
      p_fdc->current_sectors_left &= 0x1F;
      if (p_fdc->current_sectors_left == 0) {
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_gap_4);
      } else {
        intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_gap_3);
      }
    }
    break;
  case k_intel_fdc_state_format_gap_3:
    if (p_fdc->state_count < p_fdc->current_format_gap3) {
      disc_write_byte(p_current_disc, 0xFF, 0xFF);
    } else {
      disc_write_byte(p_current_disc, 0x00, 0xFF);
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == (uint32_t) (p_fdc->current_format_gap3 + 6)) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_format_write_id);
    }
    break;
  case k_intel_fdc_state_format_gap_4:
    /* GAP 4 writes until the index pulse is hit, at which point we are done. */
    if (p_fdc->state_is_index_pulse) {
      intel_fdc_set_command_result(p_fdc, 1, k_intel_fdc_result_ok);
    } else {
      disc_write_byte(p_current_disc, 0xFF, 0xFF);
    }
    break;
  default:
    assert(0);
    break;
  }

  if (p_fdc->state_is_index_pulse && !was_index_pulse) {
    p_fdc->state_index_pulse_count++;
    if (p_fdc->state != k_intel_fdc_state_idle) {
      if (p_fdc->state_index_pulse_count >= 2) {
        /* I/O commands fail with $18 (sector not found) if there are two index
         * pulses without progress.
         * EMU: interestingly enough, this applies always for an e.g. 8192 byte
         * sector read because such a crazy read cannot be satisfied within 2
         * revolutions.
         */
        intel_fdc_set_command_result(p_fdc,
                                     1,
                                     k_intel_fdc_result_sector_not_found);
      }
    } else {
      /* Idle state; check for automatic head unload. */
      if ((p_fdc->current_head_unload_count != -1) &&
          (p_fdc->state_index_pulse_count ==
               (uint32_t) p_fdc->current_head_unload_count)) {
        if (p_fdc->log_commands) {
          log_do_log(k_log_disc, k_log_info, "8271: automatic head unload");
        }
        intel_fdc_select_drive(p_fdc, 0);
      }
    }
  }
}
