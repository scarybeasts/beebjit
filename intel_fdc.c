#include "intel_fdc.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

enum {
  /* Read. */
  k_intel_fdc_status = 0,

  /* Write. */
  k_intel_fdc_command = 0,
  k_intel_fdc_parameter = 1,
};

enum {
  k_intel_fdc_max_params = 5,
};

enum {
  k_intel_fdc_command_specify = 0x35,
  k_intel_fdc_command_write_special_register = 0x3A,
};

enum {
  k_intel_fdc_register_mode = 0x17,
};

struct intel_fdc_struct {
  uint8_t status;
  uint8_t command;
  uint8_t parameters_needed;
  uint8_t parameters_index;
  uint8_t parameters[k_intel_fdc_max_params];
};

struct intel_fdc_struct*
intel_fdc_create() {
  struct intel_fdc_struct* p_intel_fdc =
      malloc(sizeof(struct intel_fdc_struct));
  if (p_intel_fdc == NULL) {
    errx(1, "couldn't allocate intel_fdc_struct");
  }
  (void) memset(p_intel_fdc, '\0', sizeof(struct intel_fdc_struct));

  p_intel_fdc->status = 0;
  p_intel_fdc->command = 0;
  p_intel_fdc->parameters_needed = 0;

  return p_intel_fdc;
}

void
intel_fdc_destroy(struct intel_fdc_struct* p_intel_fdc) {
  free(p_intel_fdc);
}

uint8_t
intel_fdc_read(struct intel_fdc_struct* p_intel_fdc, uint16_t addr) {
  switch (addr & 0x07) {
  case k_intel_fdc_status:
    return p_intel_fdc->status;
  default:
    assert(0);
    break;
  }
}

static void
intel_fdc_do_command(struct intel_fdc_struct* p_intel_fdc) {
  assert(p_intel_fdc->parameters_needed == 0);
  switch (p_intel_fdc->command) {
  case k_intel_fdc_command_specify:
    p_intel_fdc->status = 0;
    break;
  case k_intel_fdc_command_write_special_register:
    p_intel_fdc->status = 0;
    switch (p_intel_fdc->parameters[0]) {
    case k_intel_fdc_register_mode:
      break;
    default:
      assert(0);
    }
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
      /* Need parameters. Get out. */
      return;
    }
    p_intel_fdc->status = 0x80;
    p_intel_fdc->command = (val & 0x3F);
    switch (p_intel_fdc->command) {
    case 0x2C:
      num_params = 0;
      break;
    case 0x29: case 0x3D:
      num_params = 1;
      break;
    case k_intel_fdc_command_write_special_register:
      num_params = 2;
      break;
    case 0x0B: case 0x13: case 0x1B: case 0x1F:
      num_params = 3;
      break;
    case k_intel_fdc_command_specify:
      num_params = 4;
      break;
    case 0x23:
      num_params = 5;
      break;
    default:
      assert(0);
      break;
    }
    p_intel_fdc->parameters_needed = num_params;
    p_intel_fdc->parameters_index = 0;
    if (p_intel_fdc->parameters_needed == 0) {
      intel_fdc_do_command(p_intel_fdc);
    }
    break;
  case k_intel_fdc_parameter:
    if (p_intel_fdc->parameters_needed > 0) {
      p_intel_fdc->parameters[p_intel_fdc->parameters_index] = val;
      p_intel_fdc->parameters_index++;
      p_intel_fdc->parameters_needed--;
    }
    if (p_intel_fdc->parameters_needed == 0) {
      intel_fdc_do_command(p_intel_fdc);
    }
    break;
  default:
    assert(0);
  }
}
