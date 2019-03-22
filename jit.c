#include "jit.h"

#include "asm_x64_common.h"
#include "asm_x64_jit.h"
#include "bbc_options.h"
#include "cpu_driver.h"
#include "defs_6502.h"
#include "memory_access.h"
#include "jit_compiler.h"
#include "state_6502.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void* k_jit_addr = (void*) 0x20000000;
static const int k_jit_bytes_per_byte = 256;

struct jit_struct {
  struct cpu_driver driver;

  /* C callbacks called by JIT code. */
  void* p_compile_callback;

  /* 6502 address -> JIT code pointers. */
  uint32_t jit_ptrs[k_6502_addr_space_size];

  /* Fields not referenced by JIT'ed code. */
  uint8_t* p_jit_base;
  struct jit_compiler* p_compiler;
  struct util_buffer* p_temp_buf;
  struct util_buffer* p_compile_buf;

  int log_compile;
};

static uint8_t*
jit_get_jit_base_addr(struct jit_struct* p_jit, uint16_t addr_6502) {
  uint8_t* p_jit_ptr = (p_jit->p_jit_base +
                        (addr_6502 * k_jit_bytes_per_byte));
  return p_jit_ptr;
}

static void*
jit_get_block_host_address_callback(void* p, uint16_t addr_6502) {
  struct jit_struct* p_jit = (struct jit_struct*) p;
  return jit_get_jit_base_addr(p_jit, addr_6502);
}

static uint16_t
jit_get_jit_ptr_block_callback(void* p, uint32_t jit_ptr) {
  size_t ret;
  uint8_t* p_jit_ptr;

  struct jit_struct* p_jit = (struct jit_struct*) p;

  p_jit_ptr = (uint8_t*) (size_t) jit_ptr;

  ret = ((p_jit_ptr - p_jit->p_jit_base) / k_jit_bytes_per_byte);

  assert(ret < k_6502_addr_space_size);

  return ret;
}

static uint16_t
jit_6502_block_addr_from_intel(struct jit_struct* p_jit, uint8_t* p_intel_rip) {
  size_t block_addr_6502;

  uint8_t* p_jit_base = p_jit->p_jit_base;

  block_addr_6502 = (p_intel_rip - p_jit_base);
  block_addr_6502 /= k_jit_bytes_per_byte;

  assert(block_addr_6502 < k_6502_addr_space_size);

  return (uint16_t) block_addr_6502;
}

static uint16_t
jit_6502_block_addr_from_6502(struct jit_struct* p_jit, uint16_t addr) {
  void* p_intel_rip = (void*) (size_t) p_jit->jit_ptrs[addr];
  return jit_6502_block_addr_from_intel(p_jit, p_intel_rip);
}

static void
jit_init_addr(struct jit_struct* p_jit, uint16_t addr_6502) {
  struct util_buffer* p_buf = p_jit->p_temp_buf;
  uint8_t* p_jit_ptr = jit_get_jit_base_addr(p_jit, addr_6502);

  util_buffer_setup(p_buf, p_jit_ptr, 2);
  asm_x64_emit_jit_call_compile_trampoline(p_buf);
}

static void*
jit_compile(struct jit_struct* p_jit, uint8_t* p_intel_rip) {
  uint8_t* p_jit_ptr;

  struct util_buffer* p_compile_buf = p_jit->p_compile_buf;
  uint16_t addr_6502 = jit_6502_block_addr_from_intel(p_jit, p_intel_rip);

  p_jit_ptr = jit_get_jit_base_addr(p_jit, addr_6502);

  util_buffer_setup(p_compile_buf, p_jit_ptr, k_jit_bytes_per_byte);

  if (p_jit->log_compile) {
    printf("LOG:JIT:compile @ %.4X\n", addr_6502);
  }

  jit_compiler_compile_block(p_jit->p_compiler, p_compile_buf, addr_6502);

  return p_jit_ptr;
}

static void
jit_destroy(struct cpu_driver* p_cpu_driver) {
  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  size_t mapping_size = (k_6502_addr_space_size * k_jit_bytes_per_byte);

  util_buffer_destroy(p_jit->p_compile_buf);
  util_buffer_destroy(p_jit->p_temp_buf);

  jit_compiler_destroy(p_jit->p_compiler);

  util_free_guarded_mapping(k_jit_addr, mapping_size);
}

static uint32_t
jit_enter(struct cpu_driver* p_cpu_driver) {
  uint32_t ret;
  uint32_t uint_start_addr;

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  uint16_t addr_6502 = state_6502_get_pc(p_jit->driver.abi.p_state_6502);
  uint8_t* p_start_addr = jit_get_jit_base_addr(p_jit, addr_6502);
  uint_start_addr = (uint32_t) (size_t) p_start_addr;

  ret = asm_x64_asm_enter(p_jit, uint_start_addr, 0);

  return ret;
}

static char*
jit_get_address_info(struct cpu_driver* p_cpu_driver, uint16_t addr) {
  static char block_addr_buf[5];

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  uint16_t block_addr_6502 = jit_6502_block_addr_from_6502(p_jit, addr);

  (void) snprintf(block_addr_buf,
                  sizeof(block_addr_buf),
                  "%.4X",
                  block_addr_6502);

  return block_addr_buf;
}

static void
jit_init(struct cpu_driver* p_cpu_driver) {
  size_t i;
  size_t mapping_size;
  uint8_t* p_jit_base;

  struct jit_struct* p_jit = (struct jit_struct*) p_cpu_driver;
  uint8_t* p_mem_read = p_cpu_driver->p_memory_access->p_mem_read;
  struct bbc_options* p_options = p_cpu_driver->p_options;
  void* p_debug_callback_object = p_options->p_debug_callback_object;
  int debug = p_options->debug_active_at_addr(p_debug_callback_object, 0xFFFF);

  p_jit->log_compile = util_has_option(p_options->p_log_flags, "jit:compile");

  p_cpu_driver->destroy = jit_destroy;
  p_cpu_driver->enter = jit_enter;
  p_cpu_driver->get_address_info = jit_get_address_info;

  p_cpu_driver->abi.p_util_private = asm_x64_jit_compile_trampoline;
  p_jit->p_compile_callback = jit_compile;

  /* This is the mapping that holds the dynamically JIT'ed code. */
  mapping_size = (k_6502_addr_space_size * k_jit_bytes_per_byte);
  p_jit_base = util_get_guarded_mapping(k_jit_addr, mapping_size);
  util_make_mapping_read_write_exec(p_jit_base, mapping_size);

  p_jit->p_jit_base = p_jit_base;
  p_jit->p_compiler = jit_compiler_create(p_mem_read,
                                          jit_get_block_host_address_callback,
                                          jit_get_jit_ptr_block_callback,
                                          p_jit,
                                          &p_jit->jit_ptrs[0],
                                          debug);
  p_jit->p_temp_buf = util_buffer_create();
  p_jit->p_compile_buf = util_buffer_create();

  /* Fill with int3. */
  (void) memset(p_jit_base, '\xcc', mapping_size);

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    jit_init_addr(p_jit, i);
  }
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

