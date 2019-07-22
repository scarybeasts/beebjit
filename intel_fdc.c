#include "intel_fdc.h"

#include "state_6502.h"
#include "timing.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  /* Read. */
  k_intel_fdc_status = 0,
  k_intel_fdc_result = 1,

  /* Write. */
  k_intel_fdc_command = 0,
  k_intel_fdc_parameter = 1,

  /* Read / write. */
  k_intel_fdc_data = 4,
};

enum {
  k_intel_fdc_max_params = 5,
};

enum {
  k_intel_fdc_command_write_sectors = 0x0B,
  k_intel_fdc_command_read_sectors = 0x13,
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
};

enum {
  k_intel_fdc_register_scan_sector = 0x06,
  k_intel_fdc_register_mode = 0x17,
  k_intel_fdc_register_drive_out = 0x23,
};

enum {
  k_intel_fdc_sector_size = 256,
  k_intel_fdc_sectors_per_track = 10,
  k_intel_fdc_num_tracks = 80,
};

struct intel_fdc_struct {
  struct state_6502* p_state_6502;
  struct timing_struct* p_timing;
  size_t timer_id;
  uint8_t status;
  uint8_t result;
  uint8_t data;
  uint8_t drive_0_or_1;
  /* Unused except for "read drive status". */
  uint8_t drive_select;
  uint8_t current_track[2];
  uint8_t command;
  uint8_t parameters_needed;
  uint8_t parameters_index;
  uint8_t parameters[k_intel_fdc_max_params];
  uint8_t disc_data[2][k_intel_fdc_sector_size *
                       k_intel_fdc_sectors_per_track *
                       k_intel_fdc_num_tracks *
                       2];
  int disc_dsd[2];
  int disc_writeable[2];
  uint32_t disc_tracks[2];
  uint8_t current_sector;
  uint8_t current_sectors_left;
  uint16_t current_bytes_left;
  uint8_t data_command_running;
  int data_command_fire_nmi;
  uint8_t drive_out;
};

struct intel_fdc_struct*
intel_fdc_create(struct state_6502* p_state_6502,
                 struct timing_struct* p_timing) {
  struct intel_fdc_struct* p_fdc =
      malloc(sizeof(struct intel_fdc_struct));
  if (p_fdc == NULL) {
    errx(1, "couldn't allocate intel_fdc_struct");
  }
  (void) memset(p_fdc, '\0', sizeof(struct intel_fdc_struct));

  p_fdc->p_state_6502 = p_state_6502;
  p_fdc->p_timing = p_timing;

  p_fdc->status = 0;
  p_fdc->result = 0;
  p_fdc->data = 0;
  p_fdc->drive_0_or_1 = 0;
  p_fdc->drive_select = 0;
  p_fdc->current_track[0] = 0;
  p_fdc->current_track[1] = 0;
  p_fdc->command = 0;
  p_fdc->parameters_needed = 0;

  p_fdc->current_sector = 0;
  p_fdc->current_sectors_left = 0;
  p_fdc->current_bytes_left = 0;
  p_fdc->data_command_running = 0;
  p_fdc->data_command_fire_nmi = 0;
  p_fdc->drive_out = 0;

  p_fdc->disc_dsd[0] = 0;
  p_fdc->disc_dsd[1] = 0;
  p_fdc->disc_writeable[0] = 0;
  p_fdc->disc_writeable[1] = 0;
  p_fdc->disc_tracks[0] = 0;
  p_fdc->disc_tracks[1] = 0;

  p_fdc->timer_id = timing_register_timer(p_timing,
                                          intel_fdc_timer_tick,
                                          p_fdc);

  return p_fdc;
}

void
intel_fdc_load_disc(struct intel_fdc_struct* p_fdc,
                    int drive,
                    int is_dsd,
                    uint8_t* p_data,
                    size_t length,
                    int writeable) {
  uint32_t tracks;
  uint32_t track_length;
  size_t max_length;

  track_length = (k_intel_fdc_sector_size * k_intel_fdc_sectors_per_track);
  if (is_dsd) {
    /* For double sided disc images, the format is alternate sides, by track,
     * i.e. side 0 track 0, side 1 track 0, side 0 track 1, ...
     */
    track_length *= 2;
  }
  max_length = (k_intel_fdc_num_tracks * track_length);

  assert(drive == 0 || drive == 1);
  if (length > max_length) {
    errx(1, "disc image too large");
  }
  if ((length % k_intel_fdc_sector_size) != 0) {
    errx(1, "disc image not a sector multiple");
  }

  (void) memcpy(&p_fdc->disc_data[drive], p_data, length);
  p_fdc->disc_dsd[drive] = is_dsd;
  p_fdc->disc_writeable[drive] = writeable;

  /* Many images have missing sectors on the last track so pad up. */
  tracks = (length / track_length);
  if ((length % track_length) != 0) {
    tracks++;
  }
  p_fdc->disc_tracks[drive] = tracks;
}

void
intel_fdc_destroy(struct intel_fdc_struct* p_fdc) {
  free(p_fdc);
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
  if ((level == 1) && (p_fdc->data_command_running == 0)) {
    /* If we're asserting an NMI outside of a data loop, make sure there's a
     * timer set to fire immediately to ensure the main loop looks for the NMI.
     */
    struct timing_struct* p_timing = p_fdc->p_timing;
    size_t timer_id = p_fdc->timer_id;
    (void) timing_start_timer_with_value(p_timing, timer_id, 1);
  }
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
  case k_intel_fdc_data:
    intel_fdc_set_status_result(p_fdc,
                                (p_fdc->status &
                                 ~(k_intel_fdc_status_flag_need_data |
                                   k_intel_fdc_status_flag_nmi)),
                                p_fdc->result);
    return p_fdc->data;
  default:
    break;
  }
  assert(0);
  return 0;
}

static void
intel_fdc_do_command(struct intel_fdc_struct* p_fdc) {
  uint8_t temp_u8;

  uint8_t param0 = p_fdc->parameters[0];
  uint8_t param1 = p_fdc->parameters[1];
  uint8_t param2 = p_fdc->parameters[2];
  uint8_t drive_0_or_1 = p_fdc->drive_0_or_1;
  uint8_t command = p_fdc->command;
  uint8_t current_track = p_fdc->current_track[drive_0_or_1];

  assert(p_fdc->parameters_needed == 0);
  assert(p_fdc->data_command_running == 0);
  assert(p_fdc->data_command_fire_nmi == 0);
  assert(p_fdc->current_sectors_left == 0);
  assert(p_fdc->current_bytes_left == 0);

  switch (command) {
  case k_intel_fdc_command_verify_sectors:
  case k_intel_fdc_command_write_sectors:
  case k_intel_fdc_command_read_sectors:
    if ((current_track >= p_fdc->disc_tracks[drive_0_or_1]) ||
        ((p_fdc->drive_out & 0x20) && !p_fdc->disc_dsd[drive_0_or_1])) {
      intel_fdc_set_status_result(p_fdc,
                                  (k_intel_fdc_status_flag_result_ready |
                                   k_intel_fdc_status_flag_nmi),
                                  k_intel_fdc_result_sector_not_found);
      return;
    }
    break;
  default:
    break;
  }

  switch (command) {
  case k_intel_fdc_command_verify_sectors:
    /* DFS-0.9 verifies sectors before writing them. */
    intel_fdc_set_status_result(p_fdc,
                                (k_intel_fdc_status_flag_result_ready |
                                 k_intel_fdc_status_flag_nmi),
                                k_intel_fdc_result_ok);
    break;
  case k_intel_fdc_command_write_sectors:
    if (!p_fdc->disc_writeable[drive_0_or_1]) {
      intel_fdc_set_status_result(p_fdc,
                                  (k_intel_fdc_status_flag_result_ready |
                                   k_intel_fdc_status_flag_nmi),
                                  k_intel_fdc_result_write_protected);
      break;
    }
    p_fdc->data_command_fire_nmi = 1;
    /* Fall through. */
  case k_intel_fdc_command_read_sectors:
    p_fdc->current_track[drive_0_or_1] = param0;
    p_fdc->current_sector = param1;
    p_fdc->current_sectors_left = (param2 & 0x1F);
    p_fdc->current_bytes_left = k_intel_fdc_sector_size;
    p_fdc->data_command_running = command;

    (void) timing_start_timer_with_value(p_fdc->p_timing,
                                         p_fdc->timer_id,
                                         200);

    break;
  case k_intel_fdc_command_seek:
    p_fdc->current_track[drive_0_or_1] = param0;
    intel_fdc_set_status_result(p_fdc,
                                (k_intel_fdc_status_flag_result_ready |
                                 k_intel_fdc_status_flag_nmi),
                                k_intel_fdc_result_ok);
    break;
  case k_intel_fdc_command_read_drive_status:
    temp_u8 = 0x88;
    if (current_track == 0) {
      temp_u8 |= 0x02;
    }
    if (p_fdc->drive_select & 0x01) {
      temp_u8 |= 0x04;
    }
    if (p_fdc->drive_select & 0x02) {
      temp_u8 |= 0x40;
    }
    intel_fdc_set_status_result(p_fdc,
                                k_intel_fdc_status_flag_result_ready,
                                temp_u8);
    break;
  case k_intel_fdc_command_specify:
    /* EMU NOTE: different to b-em / jsbeeb. */
    intel_fdc_set_status_result(p_fdc, 0x00, 0x00);
    break;
  case k_intel_fdc_command_read_special_register:
    switch (param0) {
    case k_intel_fdc_register_scan_sector:
      /* DFS-0.9 reads this register after an 0x18 sector not found error. */
      intel_fdc_set_status_result(p_fdc,
                                  k_intel_fdc_status_flag_result_ready,
                                  0x00);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_intel_fdc_command_write_special_register:
    switch (param0) {
    case k_intel_fdc_register_mode:
      break;
    case k_intel_fdc_register_drive_out:
      /* Bit 0x20 is important as it's used to select the side of the disc for
       * double sided discs.
       */
      p_fdc->drive_out = param1;
      break;
    default:
      assert(0);
      break;
    }
    /* EMU NOTE: different to b-em / jsbeeb. */
    intel_fdc_set_status_result(p_fdc, 0x00, 0x00);
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
    if (p_fdc->status & k_intel_fdc_status_flag_busy) {
      /* Need parameters or command busy. Get out. */
      return;
    }

    assert(p_fdc->data_command_running == 0);
    assert(p_fdc->current_bytes_left == 0);
    assert(p_fdc->current_sectors_left == 0);

    p_fdc->command = (val & 0x3F);
    p_fdc->drive_select = (val >> 6);
    p_fdc->drive_0_or_1 = !!(val & 0x80);

    switch (p_fdc->command) {
    case k_intel_fdc_command_read_drive_status:
      num_params = 0;
      break;
    case k_intel_fdc_command_seek:
    case k_intel_fdc_command_read_special_register:
      num_params = 1;
      break;
    case k_intel_fdc_command_write_special_register:
      num_params = 2;
      break;
    case k_intel_fdc_command_write_sectors:
    case k_intel_fdc_command_read_sectors:
    case 0x1B:
    case k_intel_fdc_command_verify_sectors:
      num_params = 3;
      break;
    case k_intel_fdc_command_specify:
      num_params = 4;
      break;
    case k_intel_fdc_command_format:
      num_params = 5;
      break;
    default:
      intel_fdc_set_status_result(p_fdc,
                                  (k_intel_fdc_status_flag_result_ready |
                                   k_intel_fdc_status_flag_nmi),
                                  k_intel_fdc_result_sector_not_found);
      return;
    }

    p_fdc->parameters_needed = num_params;
    p_fdc->parameters_index = 0;

    if (p_fdc->parameters_needed == 0) {
      intel_fdc_do_command(p_fdc);
    } else {
      /* EMU NOTE: different to b-em / jsbeeb: sets result and NMI. */
      intel_fdc_set_status_result(p_fdc,
                                  k_intel_fdc_status_flag_busy,
                                  k_intel_fdc_result_ok);
    }
    break;
  case k_intel_fdc_parameter:
    if (p_fdc->parameters_needed > 0) {
      assert(p_fdc->data_command_running == 0);
      assert(p_fdc->current_bytes_left == 0);
      assert(p_fdc->current_sectors_left == 0);

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
    intel_fdc_set_status_result(p_fdc,
                                (p_fdc->status &
                                 ~(k_intel_fdc_status_flag_need_data |
                                   k_intel_fdc_status_flag_nmi)),
                                p_fdc->result);
    p_fdc->data = val;
    break;
  default:
    assert(0);
  }
}

static uint8_t*
intel_fdc_get_disc_byte(struct intel_fdc_struct* p_fdc,
                        uint8_t drive,
                        uint8_t track,
                        uint8_t sector,
                        uint8_t offset) {
  uint8_t* p_byte;
  uint32_t track_size;
  uint32_t track_offset;

  if ((drive != 0) && (drive != 1)) {
    return NULL;
  }
  if (track >= k_intel_fdc_num_tracks) {
    return NULL;
  }
  if (sector >= k_intel_fdc_sectors_per_track) {
    return NULL;
  }

  track_size = (k_intel_fdc_sectors_per_track * k_intel_fdc_sector_size);
  track_offset = track_size;
  if (p_fdc->disc_dsd[drive]) {
    track_offset *= 2;
  }
  track_offset *= track;
  /* If the second side of the disc is selected, select the appropriate
   * position in the file.
   */
  if (p_fdc->drive_out & 0x20) {
    track_offset += track_size;
  }

  p_byte = (uint8_t*) &p_fdc->disc_data[drive];
  p_byte += track_offset;
  p_byte += (sector * k_intel_fdc_sector_size);
  p_byte += offset;
  return p_byte;
}

void
intel_fdc_timer_tick(struct intel_fdc_struct* p_fdc) {
  uint8_t* p_byte;
  uint8_t offset;
  uint8_t drive;
  uint8_t current_sector;
  uint8_t current_sectors_left;
  uint16_t current_bytes_left;
  int fire_nmi;

  struct timing_struct* p_timing = p_fdc->p_timing;
  size_t timer_id = p_fdc->timer_id;

  if (p_fdc->data_command_running == 0) {
    /* This is a standalone NMI outside a data command. */
    (void) timing_stop_timer(p_timing, timer_id);
    return;
  }

  current_bytes_left = p_fdc->current_bytes_left;
  current_sectors_left = p_fdc->current_sectors_left;

  if (current_sectors_left == 0) {
    assert(current_bytes_left == 0);
    p_fdc->data_command_running = 0;
    (void) timing_stop_timer(p_timing, timer_id);
    intel_fdc_set_status_result(p_fdc,
                                (k_intel_fdc_status_flag_result_ready |
                                 k_intel_fdc_status_flag_nmi),
                                k_intel_fdc_result_ok);
    return;
  }

  (void) timing_adjust_timer_value(p_timing, NULL, timer_id, 200);

  if (p_fdc->data_command_fire_nmi) {
    p_fdc->data_command_fire_nmi = 0;
    intel_fdc_set_status_result(p_fdc,
                                (k_intel_fdc_status_flag_busy |
                                 k_intel_fdc_status_flag_nmi |
                                 k_intel_fdc_status_flag_need_data),
                                k_intel_fdc_result_ok);
    return;
  }

  /* If our virtual controller is attempting to crank a byte before the last
   * one was used, I presume that's data loss, otherwise what would all the
   * NMIs be about?
   * Shouldn't happen in the new timing model.
   * For now we'll be kind and give the 6502 a chance to catch up.
   */
  if (p_fdc->status & k_intel_fdc_status_flag_need_data) {
    printf("WARNING: 6502 disk byte too slow\n");
    return;
  }

  assert(current_bytes_left <= 256);
  offset = (256 - current_bytes_left);

  drive = p_fdc->drive_0_or_1;
  current_sector = p_fdc->current_sector;

  p_byte = intel_fdc_get_disc_byte(p_fdc,
                                   drive,
                                   p_fdc->current_track[drive],
                                   current_sector,
                                   offset);
  if (p_fdc->data_command_running == k_intel_fdc_command_read_sectors) {
    if (p_byte != NULL) {
      p_fdc->data = *p_byte;
    } else {
      p_fdc->data = 0xFF;
    }
    fire_nmi = 1;
  } else {
    assert(p_fdc->data_command_running ==
           k_intel_fdc_command_write_sectors);
    if (p_byte != NULL) {
      *p_byte = p_fdc->data;
    }
    if ((current_sectors_left == 1) && (current_bytes_left == 1)) {
      fire_nmi = 0;
    } else {
      fire_nmi = 1;
    }
  }

  if (fire_nmi) {
    intel_fdc_set_status_result(p_fdc,
                                (k_intel_fdc_status_flag_busy |
                                 k_intel_fdc_status_flag_nmi |
                                 k_intel_fdc_status_flag_need_data),
                                k_intel_fdc_result_ok);
  }

  current_bytes_left--;
  p_fdc->current_bytes_left = current_bytes_left;
  if (current_bytes_left != 0) {
    return;
  }

  current_sectors_left--;
  p_fdc->current_sectors_left = current_sectors_left;
  if (current_sectors_left == 0) {
    return;
  }

  p_fdc->current_sector++;
  p_fdc->current_bytes_left = k_intel_fdc_sector_size;
}
