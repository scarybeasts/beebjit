/* Appends at the end of jit.c. */

#include "test.h"

#include "bbc.h"
#include "emit_6502.h"

static struct cpu_driver* g_p_cpu_driver = NULL;
static struct jit_struct* g_p_jit = NULL;
static struct state_6502* g_p_state_6502 = NULL;
static uint8_t* g_p_mem = NULL;
static struct interp_struct* g_p_interp = NULL;
static struct jit_compiler* g_p_compiler = NULL;

static void
jit_test_init(struct bbc_struct* p_bbc) {
  struct cpu_driver* p_cpu_driver = bbc_get_cpu_driver(p_bbc);
  assert(p_cpu_driver->p_funcs->init == jit_init);

  g_p_cpu_driver = p_cpu_driver;
  g_p_jit = (struct jit_struct*) p_cpu_driver;
  g_p_state_6502 = p_cpu_driver->abi.p_state_6502;
  g_p_mem = p_cpu_driver->p_memory_access->p_mem_read;
  g_p_interp = g_p_jit->p_interp;
  g_p_compiler = g_p_jit->p_compiler;

  jit_compiler_testing_set_optimizing(g_p_compiler, 0);
  jit_compiler_testing_set_max_ops(g_p_compiler, 4);
  jit_compiler_testing_set_max_revalidate_count(g_p_compiler, 1);
}

static void
jit_test_block_split() {
  uint8_t* p_host_address;

  struct util_buffer* p_buf = util_buffer_create();

  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xB00);
  test_expect_u32(1, jit_is_host_address_invalidated(g_p_jit, p_host_address));
  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xB01);
  test_expect_u32(1, jit_is_host_address_invalidated(g_p_jit, p_host_address));

  util_buffer_setup(p_buf, (g_p_mem + 0xB00), 0x100);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_EXIT(p_buf);
  state_6502_set_pc(g_p_state_6502, 0xB00);
  jit_enter(g_p_cpu_driver);
  interp_testing_unexit(g_p_interp);

  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xB00);
  test_expect_u32(0, jit_is_host_address_invalidated(g_p_jit, p_host_address));
  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xB01);
  test_expect_u32(1, jit_is_host_address_invalidated(g_p_jit, p_host_address));

  state_6502_set_pc(g_p_state_6502, 0xB01);
  jit_enter(g_p_cpu_driver);
  interp_testing_unexit(g_p_interp);

  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xB00);
  /* TODO: should be 0? */
  test_expect_u32(1, jit_is_host_address_invalidated(g_p_jit, p_host_address));
  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xB01);
  test_expect_u32(0, jit_is_host_address_invalidated(g_p_jit, p_host_address));

  state_6502_set_pc(g_p_state_6502, 0xB00);
  jit_enter(g_p_cpu_driver);
  interp_testing_unexit(g_p_interp);

  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xB00);
  test_expect_u32(0, jit_is_host_address_invalidated(g_p_jit, p_host_address));
  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xB01);
  test_expect_u32(0, jit_is_host_address_invalidated(g_p_jit, p_host_address));

  util_buffer_destroy(p_buf);
}

static void
jit_test_block_continuation() {
  uint8_t* p_host_address;

  struct util_buffer* p_buf = util_buffer_create();

  util_buffer_setup(p_buf, (g_p_mem + 0xC00), 0x100);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  /* Block boundary here because we set the limit to 4 opcodes. */
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_EXIT(p_buf);

  state_6502_set_pc(g_p_state_6502, 0xC00);
  jit_enter(g_p_cpu_driver);
  interp_testing_unexit(g_p_interp);

  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xC00);
  test_expect_u32(0, jit_is_host_address_invalidated(g_p_jit, p_host_address));
  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xC01);
  test_expect_u32(1, jit_is_host_address_invalidated(g_p_jit, p_host_address));
  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xC04);
  test_expect_u32(0, jit_is_host_address_invalidated(g_p_jit, p_host_address));

  state_6502_set_pc(g_p_state_6502, 0xC01);
  jit_enter(g_p_cpu_driver);
  interp_testing_unexit(g_p_interp);

  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xC01);
  test_expect_u32(0, jit_is_host_address_invalidated(g_p_jit, p_host_address));
  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xC04);
  test_expect_u32(1, jit_is_host_address_invalidated(g_p_jit, p_host_address));
  p_host_address = jit_get_jit_block_host_address(g_p_jit, 0xC05);
  test_expect_u32(0, jit_is_host_address_invalidated(g_p_jit, p_host_address));
}

void
jit_test(struct bbc_struct* p_bbc) {
  jit_test_init(p_bbc);
  jit_test_block_split();
  jit_test_block_continuation();
}
