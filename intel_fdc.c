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
  k_intel_fdc_data = 4,

  /* Write. */
  k_intel_fdc_command = 0,
  k_intel_fdc_parameter = 1,
};

enum {
  k_intel_fdc_max_params = 5,
};

enum {
  k_intel_fdc_command_read_sectors = 0x13,
  k_intel_fdc_command_seek = 0x29,
  k_intel_fdc_command_read_drive_status = 0x2C,
  k_intel_fdc_command_specify = 0x35,
  k_intel_fdc_command_write_special_register = 0x3A,
};

enum {
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
  uint8_t disc_data[2][k_intel_fdc_num_tracks *
                       k_intel_fdc_sectors_per_track *
                       k_intel_fdc_num_tracks];
  uint8_t current_sector;
  uint8_t current_sectors_left;
  uint16_t current_bytes_left;
  uint8_t data_command_running;
};

struct intel_fdc_struct*
intel_fdc_create(struct state_6502* p_state_6502,
                 struct timing_struct* p_timing) {
  struct intel_fdc_struct* p_intel_fdc =
      malloc(sizeof(struct intel_fdc_struct));
  if (p_intel_fdc == NULL) {
    errx(1, "couldn't allocate intel_fdc_struct");
  }
  (void) memset(p_intel_fdc, '\0', sizeof(struct intel_fdc_struct));

  p_intel_fdc->p_state_6502 = p_state_6502;
  p_intel_fdc->p_timing = p_timing;

  p_intel_fdc->status = 0;
  p_intel_fdc->result = 0;
  p_intel_fdc->data = 0;
  p_intel_fdc->drive_0_or_1 = 0;
  p_intel_fdc->drive_select = 0;
  p_intel_fdc->current_track[0] = 0;
  p_intel_fdc->current_track[1] = 0;
  p_intel_fdc->command = 0;
  p_intel_fdc->parameters_needed = 0;

  p_intel_fdc->current_sector = 0;
  p_intel_fdc->current_sectors_left = 0;
  p_intel_fdc->current_bytes_left = 0;
  p_intel_fdc->data_command_running = 0;

  p_intel_fdc->timer_id = timing_register_timer(p_timing,
                                                intel_fdc_timer_tick,
                                                p_intel_fdc);

  return p_intel_fdc;
}

void
intel_fdc_load_ssd(struct intel_fdc_struct* p_fdc,
                   int drive,
                   uint8_t* p_data,
                   size_t length) {
  size_t max_length = (k_intel_fdc_num_tracks *
                       k_intel_fdc_sectors_per_track *
                       k_intel_fdc_sector_size);

  assert(drive == 0 || drive == 1);
  if (length > max_length) {
    length = max_length;
  }

  (void) memcpy(&p_fdc->disc_data[drive], p_data, length);
}

void
intel_fdc_destroy(struct intel_fdc_struct* p_intel_fdc) {
  free(p_intel_fdc);
}

static void
intel_fdc_set_status_result(struct intel_fdc_struct* p_intel_fdc,
                            uint8_t status,
                            uint8_t result) {
  struct state_6502* p_state_6502 = p_intel_fdc->p_state_6502;
  int level = !!(status & 0x08);
  int firing = state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi);

  p_intel_fdc->status = status;
  p_intel_fdc->result = result;

  if (firing && (level == 1)) {
    printf("WARNING: edge triggered NMI already high\n");
  }

  state_6502_set_irq_level(p_intel_fdc->p_state_6502,
                           k_state_6502_irq_nmi,
                           level);
  if ((level == 1) && (p_intel_fdc->data_command_running == 0)) {
    /* If we're asserting an NMI outside of a data loop, make sure there's a
     * timer set to fire immediately to ensure the main loop looks for the NMI.
     */
    struct timing_struct* p_timing = p_intel_fdc->p_timing;
    size_t timer_id = p_intel_fdc->timer_id;
    assert(!timing_timer_is_running(p_timing, timer_id));
    (void) timing_start_timer(p_timing, timer_id, 0);
  }
}

uint8_t
intel_fdc_read(struct intel_fdc_struct* p_intel_fdc, uint16_t addr) {
  switch (addr & 0x07) {
  case k_intel_fdc_status:
    return p_intel_fdc->status;
  case k_intel_fdc_result:
    intel_fdc_set_status_result(p_intel_fdc,
                                (p_intel_fdc->status & ~0x18),
                                p_intel_fdc->result);
    return p_intel_fdc->result;
  case k_intel_fdc_data:
    intel_fdc_set_status_result(p_intel_fdc,
                                (p_intel_fdc->status & ~0x0C),
                                p_intel_fdc->result);
    return p_intel_fdc->data;
  default:
    assert(0);
    break;
  }
}

static void
intel_fdc_do_command(struct intel_fdc_struct* p_intel_fdc) {
  uint8_t temp_u8;

  uint8_t param0 = p_intel_fdc->parameters[0];
  uint8_t param1 = p_intel_fdc->parameters[1];
  uint8_t param2 = p_intel_fdc->parameters[2];

  assert(p_intel_fdc->parameters_needed == 0);
  assert(p_intel_fdc->data_command_running == 0);
  assert(p_intel_fdc->current_sectors_left == 0);
  assert(p_intel_fdc->current_bytes_left == 0);

  switch (p_intel_fdc->command) {
  case k_intel_fdc_command_read_sectors:
    p_intel_fdc->current_track[p_intel_fdc->drive_0_or_1] = param0;
    p_intel_fdc->current_sector = param1;
    p_intel_fdc->current_sectors_left = (param2 & 0x1F);
    p_intel_fdc->current_bytes_left = k_intel_fdc_sector_size;
    p_intel_fdc->data_command_running = 1;

    (void) timing_start_timer(p_intel_fdc->p_timing,
                              p_intel_fdc->timer_id,
                              200);

    break;
  case k_intel_fdc_command_seek:
    p_intel_fdc->current_track[p_intel_fdc->drive_0_or_1] = param0;
    intel_fdc_set_status_result(p_intel_fdc, 0x18, 0x00);
    break;
  case k_intel_fdc_command_read_drive_status:
    temp_u8 = 0x88;
    if (!p_intel_fdc->current_track[p_intel_fdc->drive_0_or_1]) {
      temp_u8 |= 0x02;
    }
    if (p_intel_fdc->drive_select & 0x01) {
      temp_u8 |= 0x04;
    }
    if (p_intel_fdc->drive_select & 0x02) {
      temp_u8 |= 0x40;
    }
    intel_fdc_set_status_result(p_intel_fdc, 0x10, temp_u8);
    break;
  case k_intel_fdc_command_specify:
    /* EMU NOTE: different to b-em / jsbeeb. */
    intel_fdc_set_status_result(p_intel_fdc, 0x00, 0x00);
    break;
  case k_intel_fdc_command_write_special_register:
    switch (p_intel_fdc->parameters[0]) {
    case k_intel_fdc_register_mode:
      break;
    case k_intel_fdc_register_drive_out:
      /* Looks to be a bitfield, where 0x20 is double density select? We can
       * likely safely ignore for now.
       */
      break;
    default:
      assert(0);
    }
    /* EMU NOTE: different to b-em / jsbeeb. */
    intel_fdc_set_status_result(p_intel_fdc, 0x00, 0x00);
    break;
  default:
    assert(0);
  }
}

void
intel_fdc_write(struct intel_fdc_struct* p_intel_fdc,
                uint16_t addr,
                uint8_t val) {
  uint8_t num_params;

  switch (addr & 0x07) {
  case k_intel_fdc_command:
    if (p_intel_fdc->status & 0x80) {
      /* Need parameters or command busy. Get out. */
      return;
    }

    assert(p_intel_fdc->data_command_running == 0);
    assert(p_intel_fdc->current_bytes_left == 0);
    assert(p_intel_fdc->current_sectors_left == 0);

    p_intel_fdc->command = (val & 0x3F);
    p_intel_fdc->drive_select = (val >> 6);
    p_intel_fdc->drive_0_or_1 = !!(val & 0x80);

    switch (p_intel_fdc->command) {
    case k_intel_fdc_command_read_drive_status:
      num_params = 0;
      break;
    case k_intel_fdc_command_seek:
    case 0x3D:
      num_params = 1;
      break;
    case k_intel_fdc_command_write_special_register:
      num_params = 2;
      break;
    case 0x0B:
    case k_intel_fdc_command_read_sectors:
    case 0x1B:
    case 0x1F:
      num_params = 3;
      break;
    case k_intel_fdc_command_specify:
      num_params = 4;
      break;
    case 0x23:
      num_params = 5;
      break;
    default:
      intel_fdc_set_status_result(p_intel_fdc, 0x18, 0x18);
      return;
    }

    p_intel_fdc->parameters_needed = num_params;
    p_intel_fdc->parameters_index = 0;

    if (p_intel_fdc->parameters_needed == 0) {
      intel_fdc_do_command(p_intel_fdc);
    } else {
      /* EMU NOTE: different to b-em / jsbeeb: sets result and NMI. */
      intel_fdc_set_status_result(p_intel_fdc, 0x80, 0x00);
    }
    break;
  case k_intel_fdc_parameter:
    if (p_intel_fdc->parameters_needed > 0) {
      assert(p_intel_fdc->data_command_running == 0);
      assert(p_intel_fdc->current_bytes_left == 0);
      assert(p_intel_fdc->current_sectors_left == 0);

      p_intel_fdc->parameters[p_intel_fdc->parameters_index] = val;
      p_intel_fdc->parameters_index++;
      p_intel_fdc->parameters_needed--;
    }
    if (p_intel_fdc->parameters_needed == 0 && p_intel_fdc->status == 0x80) {
      intel_fdc_do_command(p_intel_fdc);
    }
    break;
  default:
    assert(0);
  }
}

static uint8_t
intel_fdc_get_disc_byte(struct intel_fdc_struct* p_intel_fdc,
                        uint8_t drive,
                        uint8_t track,
                        uint8_t sector,
                        uint8_t offset) {
  uint8_t* p_byte;

  if (drive != 0 && drive != 1) {
    return 0xFF;
  }
  if (track >= k_intel_fdc_num_tracks) {
    return 0xFF;
  }
  if (sector >= k_intel_fdc_sectors_per_track) {
    return 0xFF;
  }

  p_byte = (uint8_t*) &p_intel_fdc->disc_data[drive];
  p_byte += (track * k_intel_fdc_sectors_per_track * k_intel_fdc_sector_size);
  p_byte += (sector * k_intel_fdc_sector_size);
  p_byte += offset;
  return *p_byte;
}

void
intel_fdc_timer_tick(struct intel_fdc_struct* p_intel_fdc) {
  uint8_t byte;
  uint8_t offset;
  uint8_t drive;
  uint8_t current_sector;
  uint8_t current_sectors_left;
  uint16_t current_bytes_left;

  struct timing_struct* p_timing = p_intel_fdc->p_timing;
  size_t timer_id = p_intel_fdc->timer_id;

  if (p_intel_fdc->data_command_running == 0) {
    /* This is a standalone NMI outside a data command. */
    (void) timing_stop_timer(p_timing, timer_id);
    return;
  }

  current_bytes_left = p_intel_fdc->current_bytes_left;
  current_sectors_left = p_intel_fdc->current_sectors_left;

  if (current_sectors_left == 0) {
    assert(current_bytes_left == 0);
    p_intel_fdc->data_command_running = 0;
    (void) timing_stop_timer(p_timing, timer_id);
    intel_fdc_set_status_result(p_intel_fdc, 0x18, 0x00);
    return;
  }

  (void) timing_increase_timer(NULL, p_timing, timer_id, 200);

  /* If our virtual controller is attempting to deliver a byte before the last
   * one was read, I presume that's data loss, otherwise what would all the
   * NMIs be about?
   * Shouldn't happen in the new timing model.
   * For now we'll be kind and give the 6502 a chance to catch up.
   */
  if (p_intel_fdc->status & 0x04) {
    printf("WARNING: 6502 disk byte read too slow\n");
    return;
  }

  assert(current_bytes_left <= 256);
  offset = (256 - current_bytes_left);

  drive = p_intel_fdc->drive_0_or_1;
  current_sector = p_intel_fdc->current_sector;

  byte = intel_fdc_get_disc_byte(p_intel_fdc,
                                 drive,
                                 p_intel_fdc->current_track[drive],
                                 current_sector,
                                 offset);
  p_intel_fdc->data = byte;

  intel_fdc_set_status_result(p_intel_fdc, 0x8C, 0x00);

  current_bytes_left--;
  p_intel_fdc->current_bytes_left = current_bytes_left;
  if (current_bytes_left != 0) {
    return;
  }

  current_sectors_left--;
  p_intel_fdc->current_sectors_left = current_sectors_left;
  if (current_sectors_left == 0) {
    return;
  }

  p_intel_fdc->current_sector++;
  p_intel_fdc->current_bytes_left = k_intel_fdc_sector_size;
}
