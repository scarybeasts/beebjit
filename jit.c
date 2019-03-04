#include "jit.h"

#include "asm_x64_common.h"
#include "cpu_driver.h"
#include "defs_6502.h"
#include "state_6502.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

static void* k_jit_addr = (void*) 0x20000000;
static const int k_jit_bytes_per_byte = 256;

struct jit_struct {
  struct cpu_driver driver;

  /* C callbacks called by JIT code. */
  void* p_jit_callback;

  /* Fields not referenced by JIT'ed code. */
  uint8_t* p_jit_base;
};

static void
jit_destroy(struct cpu_driver* p_cpu_driver) {
  (void) p_cpu_driver;
  assert(0);
}

static uint32_t
jit_enter(struct cpu_driver* p_cpu_driver) {
  uint32_t ret;
  uint32_t uint_start_addr;

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  uint16_t reg_pc = state_6502_get_pc(p_jit->driver.abi.p_state_6502);
  uint8_t* p_start_addr = p_jit->p_jit_base;
  p_start_addr += (reg_pc * k_jit_bytes_per_byte);
  uint_start_addr = (uint32_t) (size_t) p_start_addr;

  ret = asm_x64_asm_enter(p_jit, uint_start_addr, 0);

  return ret;
}

static void
jit_init(struct cpu_driver* p_cpu_driver) {
  size_t mapping_size;
  uint8_t* p_jit_base;

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;

  p_cpu_driver->destroy = jit_destroy;
  p_cpu_driver->enter = jit_enter;

  /* This is the mapping that holds the dynamically JIT'ed code. */
  mapping_size = (k_6502_addr_space_size * k_jit_bytes_per_byte);
  p_jit_base = util_get_guarded_mapping( k_jit_addr, mapping_size);
  util_make_mapping_read_write_exec(p_jit_base, mapping_size);

  p_jit->p_jit_base = p_jit_base;

  /* Fill with int3. */
  (void) memset(p_jit_base, '\xcc', mapping_size);
}

struct cpu_driver*
jit_create() {
  struct cpu_driver* p_cpu_driver = malloc(sizeof(struct jit_struct));
  if (p_cpu_driver == NULL) {
    errx(1, "cannot allocate jit_struct");
  }
  (void) memset(p_cpu_driver, '\0', sizeof(struct jit_struct));

  p_cpu_driver->init = jit_init;

  return p_cpu_driver;
}

