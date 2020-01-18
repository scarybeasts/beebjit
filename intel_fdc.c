#include "intel_fdc.h"

#include "bbc_options.h"
#include "disc.h"
#include "ibm_disc_format.h"
#include "log.h"
#include "state_6502.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  k_intel_fdc_result_write_protected = 0x12,
  k_intel_fdc_result_sector_not_found = 0x18,
  k_intel_fdc_result_flag_deleted_data = 0x20,
};

enum {
  k_intel_fdc_register_scan_sector = 0x06,
  k_intel_fdc_register_track_drive_0 = 0x12,
  k_intel_fdc_register_mode = 0x17,
  k_intel_fdc_register_track_drive_1 = 0x1A,
  k_intel_fdc_register_drive_out = 0x23,
};

enum {
  k_intel_fdc_drive_out_side = 0x20,
  k_intel_fdc_drive_out_load_head = 0x08,
};

enum {
  k_intel_fdc_state_idle = 0,
  k_intel_fdc_state_wait_no_index = 1,
  k_intel_fdc_state_wait_index = 2,
  k_intel_fdc_state_prepare_search_id = 3,
  k_intel_fdc_state_search_id = 4,
  k_intel_fdc_state_in_id = 5,
  k_intel_fdc_state_in_id_crc = 6,
  k_intel_fdc_state_search_data = 7,
  k_intel_fdc_state_in_data = 8,
  k_intel_fdc_state_in_deleted_data = 9,
  k_intel_fdc_state_in_data_crc = 10,
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

  uint8_t command_track;
  uint8_t command_sector;
  uint8_t command_num_sectors;
  uint32_t command_sector_size;
  int command_is_transfer_deleted;

  uint8_t current_sector;
  uint8_t current_sectors_left;
  int current_had_deleted_data;

  int state;
  uint32_t state_count;
  int state_is_index_pulse;
  uint32_t state_index_pulse_count;
  uint8_t state_id_track;
  uint8_t state_id_sector;
  uint16_t crc;
};

struct intel_fdc_struct*
intel_fdc_create(struct state_6502* p_state_6502,
                 struct bbc_options* p_options) {
  struct intel_fdc_struct* p_fdc =
      malloc(sizeof(struct intel_fdc_struct));
  if (p_fdc == NULL) {
    errx(1, "couldn't allocate intel_fdc_struct");
  }
  (void) memset(p_fdc, '\0', sizeof(struct intel_fdc_struct));

  p_fdc->p_state_6502 = p_state_6502;

  p_fdc->log_commands = util_has_option(p_options->p_log_flags,
                                        "disc:commands");

  p_fdc->state = k_intel_fdc_state_idle;

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
}

void
intel_fdc_destroy(struct intel_fdc_struct* p_fdc) {
  /* TODO: stop discs if spinning? */
  free(p_fdc);
}

static inline void
intel_fdc_set_state(struct intel_fdc_struct* p_fdc, int state) {
  p_fdc->state = state;
  p_fdc->state_count = 0;
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
    printf("WARNING: edge triggered NMI already high\n");
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
               "8271: status %x result %x",
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
   * For example, the newly select drive won't have the load head signal
   * active.
   * This also spins down the previously selected drive.
   */
  new_drive_out = (p_fdc->drive_out & ~0xC0);
  new_drive_out |= new_drive_select;
  new_drive_out &= ~k_intel_fdc_drive_out_load_head;
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

static void
intel_fdc_do_seek(struct intel_fdc_struct* p_fdc, uint8_t seek_to) {
  int32_t delta;
  uint8_t* p_logical_track;

  struct disc_struct* p_current_disc = p_fdc->p_current_disc;
  if (p_current_disc == NULL) {
    return;
  }

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
  if (seek_to == 0) {
    delta = -0xFF;
  } else {
    delta = (seek_to - *p_logical_track);
  }
  disc_seek_track(p_current_disc, delta);

  *p_logical_track = seek_to;
}

static void
intel_fdc_do_command(struct intel_fdc_struct* p_fdc) {
  uint8_t temp_u8;
  uint8_t command;

  uint8_t param0 = p_fdc->parameters[0];
  uint8_t param1 = p_fdc->parameters[1];
  uint8_t param2 = p_fdc->parameters[2];
  int do_seek = 0;

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

  intel_fdc_select_drive(p_fdc, (p_fdc->command_pending & 0xC0));

  if (p_fdc->log_commands) {
    log_do_log(k_log_disc,
               k_log_info,
               "8271: command %x select %x params %x %x %x",
               command,
               p_fdc->drive_select,
               param0,
               param1,
               param2);
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
    intel_fdc_set_drive_out(p_fdc,
                            (p_fdc->drive_out |
                                k_intel_fdc_drive_out_load_head));
    do_seek = 1;
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
  default:
    break;
  }

  if (do_seek) {
    intel_fdc_do_seek(p_fdc, param0);
  }

  switch (command) {
  case k_intel_fdc_command_read_sector_with_deleted_128:
  case k_intel_fdc_command_read_sectors_with_deleted:
    p_fdc->command_is_transfer_deleted = 1;
    break;
  default:
    break;
  }

  p_fdc->current_had_deleted_data = 0;

  switch (command) {
  case k_intel_fdc_command_read_sector_128:
  case k_intel_fdc_command_read_sectors:
  case k_intel_fdc_command_read_sector_with_deleted_128:
  case k_intel_fdc_command_read_sectors_with_deleted:
    p_fdc->current_sector = p_fdc->command_sector;
    p_fdc->current_sectors_left = p_fdc->command_num_sectors;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_prepare_search_id);
    break;
  case k_intel_fdc_command_read_sector_ids:
    p_fdc->current_sectors_left = p_fdc->command_num_sectors;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_wait_no_index);
    break;
  case k_intel_fdc_command_seek:
    intel_fdc_set_command_result(p_fdc, 1, k_intel_fdc_result_ok);
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
    /* EMU: return value matches real 8271. */
    intel_fdc_set_command_result(p_fdc, 0, k_intel_fdc_result_ok);
    break;
  case k_intel_fdc_command_read_special_register:
    temp_u8 = 0;
    switch (param0) {
    case k_intel_fdc_register_scan_sector:
      /* DFS-0.9 reads this register after an 0x18 sector not found error. */
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
    switch (param0) {
    case k_intel_fdc_register_track_drive_0:
      p_fdc->logical_track[0] = param1;
      break;
    case k_intel_fdc_register_mode:
      break;
    case k_intel_fdc_register_track_drive_1:
      p_fdc->logical_track[1] = param1;
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
      intel_fdc_select_drive(p_fdc, (param1 & 0xC0));
      intel_fdc_set_drive_out(p_fdc, param1);
      break;
    default:
      assert(0);
      break;
    }
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
      errx(1, "unimplemented 8271 command %x", (val & 0x3F));
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
    if (p_fdc->parameters_needed > 0) {
      assert(p_fdc->status & k_intel_fdc_status_flag_busy);
      assert(p_fdc->state == k_intel_fdc_state_idle);
      assert(p_fdc->state_count == 0);
      assert(p_fdc->command == 0);

      p_fdc->parameters[p_fdc->parameters_index] = val;
      p_fdc->parameters_index++;
      p_fdc->parameters_needed--;
    }
    if ((p_fdc->parameters_needed == 0) &&
        (p_fdc->status == k_intel_fdc_status_flag_busy)) {
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
  default:
    assert(0);
    break;
  }
}

static void
intel_fdc_crc_init(struct intel_fdc_struct* p_fdc) {
  p_fdc->crc = 0xFFFF;
}

static void
intel_fdc_crc_add_byte(struct intel_fdc_struct* p_fdc, uint8_t byte) {
  (void) p_fdc;
  (void) byte;
}

static int
intel_fdc_provide_data_byte(struct intel_fdc_struct* p_fdc, uint8_t byte) {
  /* TODO: if byte wasn't consumed, return error $0A. */
  p_fdc->data = byte;
  intel_fdc_set_status_result(p_fdc,
                              (k_intel_fdc_status_flag_busy |
                                  k_intel_fdc_status_flag_nmi |
                                  k_intel_fdc_status_flag_need_data),
                                  k_intel_fdc_result_ok);
  return 1;
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
    /* EMU: Go to state prepare ID so that the index pulse counter is reset.
     * A real 8271 permits two index pulses per sector, not per command.
     */
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_prepare_search_id);
  }
}

void
intel_fdc_byte_callback(void* p, uint8_t data_byte, uint8_t clocks_byte) {
  int was_index_pulse;

  struct intel_fdc_struct* p_fdc = (struct intel_fdc_struct*) p;

  was_index_pulse =  p_fdc->state_is_index_pulse;
  p_fdc->state_is_index_pulse = intel_fdc_get_INDEX(p_fdc);

  switch (p_fdc->state) {
  case k_intel_fdc_state_idle:
    break;
  case k_intel_fdc_state_wait_no_index:
    p_fdc->state_index_pulse_count = 0;
    if (!p_fdc->state_is_index_pulse) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_wait_index);
    }
    break;
  case k_intel_fdc_state_wait_index:
    p_fdc->state_index_pulse_count = 0;
    if (p_fdc->state_is_index_pulse) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_prepare_search_id);
    }
    break;
  case k_intel_fdc_state_prepare_search_id:
    p_fdc->state_index_pulse_count = 0;
    intel_fdc_set_state(p_fdc, k_intel_fdc_state_search_id);
    break;
  case k_intel_fdc_state_search_id:
    if ((clocks_byte == k_ibm_disc_mark_clock_pattern) &&
        (data_byte == k_ibm_disc_id_mark_data_pattern)) {
      intel_fdc_crc_init(p_fdc);
      intel_fdc_crc_add_byte(p_fdc, k_ibm_disc_id_mark_data_pattern);

      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_id);
    }
    break;
  case k_intel_fdc_state_in_id:
    intel_fdc_crc_add_byte(p_fdc, data_byte);
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
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_id_crc);
    }
    break;
  case k_intel_fdc_state_in_id_crc:
    p_fdc->state_count++;
    if (p_fdc->state_count == 2) {
      /* TODO: check CRC of course. */
      if (p_fdc->command == k_intel_fdc_command_read_sector_ids) {
        intel_fdc_check_completion(p_fdc);
      } else {
        if ((p_fdc->state_id_track == p_fdc->command_track) &&
            (p_fdc->state_id_sector == p_fdc->current_sector)) {
          intel_fdc_set_state(p_fdc, k_intel_fdc_state_search_data);
        } else {
          intel_fdc_set_state(p_fdc, k_intel_fdc_state_search_id);
        }
      }
    }
    break;
  case k_intel_fdc_state_search_data:
    /* EMU TODO: what happens if the controller hits another ID mark before it
     * ever sees a data mark?
     */
    if ((clocks_byte == k_ibm_disc_mark_clock_pattern) &&
        ((data_byte == k_ibm_disc_data_mark_data_pattern) ||
            (data_byte == k_ibm_disc_deleted_data_mark_data_pattern))) {
      int new_state = k_intel_fdc_state_in_data;
      if (data_byte == k_ibm_disc_deleted_data_mark_data_pattern) {
        p_fdc->current_had_deleted_data = 1;
        new_state = k_intel_fdc_state_in_deleted_data;
      }
      intel_fdc_crc_init(p_fdc);
      intel_fdc_crc_add_byte(p_fdc, data_byte);

      intel_fdc_set_state(p_fdc, new_state);
    }
    break;
  case k_intel_fdc_state_in_data:
    intel_fdc_crc_add_byte(p_fdc, data_byte);
    if (!intel_fdc_provide_data_byte(p_fdc, data_byte)) {
      break;
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == p_fdc->command_sector_size) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_data_crc);
    }
    break;
  case k_intel_fdc_state_in_deleted_data:
    intel_fdc_crc_add_byte(p_fdc, data_byte);
    if (p_fdc->command_is_transfer_deleted) {
      if (!intel_fdc_provide_data_byte(p_fdc, data_byte)) {
        break;
      }
    }
    p_fdc->state_count++;
    if (p_fdc->state_count == p_fdc->command_sector_size) {
      intel_fdc_set_state(p_fdc, k_intel_fdc_state_in_data_crc);
    }
    break;
  case k_intel_fdc_state_in_data_crc:
    p_fdc->state_count++;
    if (p_fdc->state_count == 2) {
      /* TODO: check CRC of course. */
      intel_fdc_check_completion(p_fdc);
    }
    break;
  default:
    assert(0);
    break;
  }

  if (p_fdc->state_is_index_pulse && !was_index_pulse) {
    p_fdc->state_index_pulse_count++;
    if ((p_fdc->state != k_intel_fdc_state_idle) &&
        (p_fdc->state_index_pulse_count >= 2)) {
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
  }
}
