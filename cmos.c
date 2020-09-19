#include "cmos.h"

#include "bbc_options.h"
#include "log.h"
#include "util.h"

#include <assert.h>

enum {
  k_cmos_port_b_address_strobe = 0x80,
  k_cmos_port_b_enable = 0x40,
};

enum {
  k_cmos_IC32_data = 4,
  k_cmos_IC32_read = 2,
};

/* From jsbeeb. */
static const uint8_t s_cmos_defaults[64] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xc9, 0xff, 0xff, 0x12, 0x00,
    0x17, 0xca, 0x1e, 0x05, 0x00, 0x35, 0xa6, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

struct cmos_struct {
  int log;

  int enabled;
  int address_strobe;
  int data;
  int read;
  uint8_t addr;
};

struct cmos_struct*
cmos_create(struct bbc_options* p_options) {
  struct cmos_struct* p_cmos = util_mallocz(sizeof(struct cmos_struct));

  p_cmos->log = util_has_option(p_options->p_log_flags, "cmos:all");

  p_cmos->enabled = 0;
  p_cmos->address_strobe = 0;
  p_cmos->data = 0;
  p_cmos->read = 0;

  return p_cmos;
}

void cmos_destroy(struct cmos_struct* p_cmos) {
  util_free(p_cmos);
}

uint8_t
cmos_get_bus_value(struct cmos_struct* p_cmos) {
  uint8_t val = 0xFF;

  assert(p_cmos->addr < 64);

  if (p_cmos->enabled &&
      !p_cmos->address_strobe &&
      p_cmos->data &&
      p_cmos->read) {
    uint8_t addr = p_cmos->addr;
    val = s_cmos_defaults[addr];

    if (p_cmos->log) {
      log_do_log(k_log_cmos,
                 k_log_info,
                 "address %.2X value %.2X on bus",
                 addr,
                 val);
    }
  }

  return val;
}

void
cmos_update_external_inputs(struct cmos_struct* p_cmos,
                            uint8_t port_b,
                            uint8_t port_a,
                            uint8_t IC32) {
  int enabled = !!(port_b & k_cmos_port_b_enable);
  int new_address_strobe = !!(port_b & k_cmos_port_b_address_strobe);
  int new_data = !!(IC32 & k_cmos_IC32_data);
  int new_read = !!(IC32 & k_cmos_IC32_read);

  p_cmos->enabled = enabled;
  if (!enabled) {
    return;
  }

  /* The address strobe high -> low edge triggers an address latch. */
  if (!new_address_strobe && p_cmos->address_strobe) {
    p_cmos->addr = (port_a & 0x3F);

    if (p_cmos->log) {
      log_do_log(k_log_cmos, k_log_info, "new address %.2X", p_cmos->addr);
    }
  }
  p_cmos->address_strobe = new_address_strobe;

  /* The data pin high -> in write mode does a write. */
  if (!new_data && !p_cmos->data && !new_address_strobe && !new_read) {
    /* TODO: don't forget to look a filtering write to <= 0x0B. */
    log_do_log(k_log_cmos,
               k_log_unimplemented,
               "WRITE address %.2X value %.2X",
               p_cmos->addr,
               port_a);
  }
  
  p_cmos->data = new_data;
  p_cmos->read = new_read;

  if (p_cmos->log) {
    log_do_log(k_log_cmos,
               k_log_info,
               "enabled %d address_strobe %d data %d read %d",
               p_cmos->enabled,
               p_cmos->address_strobe,
               p_cmos->data,
               p_cmos->read);
  }
}
