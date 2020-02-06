/* Appends at the end of jit.c. */

#include "test.h"

#include "bbc.h"
#include "emit_6502.h"

static struct cpu_driver* g_p_cpu_driver = NULL;
static struct jit_struct* g_p_jit = NULL;
static struct state_6502* g_p_state_6502 = NULL;
static uint8_t* g_p_mem = NULL;

static void
jit_test_init(struct bbc_struct* p_bbc) {
  struct cpu_driver* p_cpu_driver = bbc_get_cpu_driver(p_bbc);
  assert(p_cpu_driver->p_funcs->init == jit_init);

  g_p_cpu_driver = p_cpu_driver;
  g_p_jit = (struct jit_struct*) p_cpu_driver;
  g_p_state_6502 = p_cpu_driver->abi.p_state_6502;
  g_p_mem = p_cpu_driver->p_memory_access->p_mem_read;
}

static void
jit_test_block_split() {
  struct util_buffer* p_buf = util_buffer_create();

  util_buffer_setup(p_buf, (g_p_mem + 0xB00), 0x100);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_EXIT(p_buf);
  state_6502_set_pc(g_p_state_6502, 0xB00);
  jit_enter(g_p_cpu_driver);

  util_buffer_destroy(p_buf);
}

void
jit_test(struct bbc_struct* p_bbc) {
  jit_test_init(p_bbc);
  jit_test_block_split();
}
