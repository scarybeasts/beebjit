/* Appends at the end of jit.c. */

#include "test.h"

#include "bbc.h"
#include "emit_6502.h"
/* For jit_has_invalidated_code(). */
#include "jit_compiler.h"

static struct cpu_driver* s_p_cpu_driver = NULL;
static struct jit_struct* s_p_jit = NULL;
static struct state_6502* s_p_state_6502 = NULL;
static uint8_t* s_p_mem = NULL;
static struct interp_struct* s_p_interp = NULL;
static struct jit_compiler* s_p_compiler = NULL;
static struct timing_struct* s_p_timing = NULL;

static void
jit_test_invalidate_code_at_address(struct jit_struct* p_jit, uint16_t addr) {
  asm_jit_start_code_updates(p_jit->p_asm, NULL, 0);
  jit_invalidate_code_at_address(p_jit, addr);
  asm_jit_finish_code_updates(p_jit->p_asm);
}

static int
jit_is_jit_ptr_dyanmic(struct jit_struct* p_jit, uint16_t addr_6502) {
  void* p_jit_ptr = jit_get_host_jit_ptr(p_jit, addr_6502);
  return (p_jit_ptr == p_jit->p_jit_ptr_dynamic_operand);
}

static void
jit_test_expect_block_invalidated(int expect, uint16_t block_addr) {
  void* p_host_address = jit_get_jit_block_host_address(s_p_jit, block_addr);
  test_expect_u32(expect, asm_jit_is_invalidated_code_at(p_host_address));
}

static void
jit_test_expect_code_invalidated(int expect, uint16_t code_addr) {
  void* p_host_address = jit_get_host_jit_ptr(s_p_jit, code_addr);
  test_expect_u32(expect, asm_jit_is_invalidated_code_at(p_host_address));
}

static void
jit_test_init(struct bbc_struct* p_bbc) {
  struct cpu_driver* p_cpu_driver = bbc_get_cpu_driver(p_bbc);
  struct timing_struct* p_timing = bbc_get_timing(p_bbc);

  assert(p_cpu_driver->p_funcs->init == jit_init);

  /* Timers firing interfere with the tests.
   * Make sure the video subsystem is in a reasonable state -- the timing code
   * has historically varied a lot!
   */
  assert(timing_get_countdown(p_timing) > 10000);

  s_p_cpu_driver = p_cpu_driver;
  s_p_jit = (struct jit_struct*) p_cpu_driver;
  s_p_timing = p_timing;
  s_p_state_6502 = p_cpu_driver->abi.p_state_6502;
  s_p_mem = p_cpu_driver->p_extra->p_memory_access->p_mem_read;
  s_p_interp = s_p_jit->p_interp;
  s_p_compiler = s_p_jit->p_compiler;

  jit_compiler_testing_set_optimizing(s_p_compiler, 0);
  jit_compiler_testing_set_dynamic_operand(s_p_compiler, 0);
  jit_compiler_testing_set_dynamic_opcode(s_p_compiler, 0);
  jit_compiler_testing_set_sub_instruction(s_p_compiler, 0);
  jit_compiler_testing_set_max_ops(s_p_compiler, 4);
  jit_compiler_testing_set_dynamic_trigger(s_p_compiler, 1);
  jit_compiler_testing_set_accurate_cycles(s_p_compiler, 1);
}

static void
jit_test_details_from_host_ip(void) {
  uint32_t i;
  struct jit_host_ip_details details;
  void* p_jit_ptr;
  struct util_buffer* p_buf = util_buffer_create();
  uint8_t* p_200_host_block = jit_get_jit_block_host_address(s_p_jit, 0x0200);
  uint8_t* p_A00_host_block = jit_get_jit_block_host_address(s_p_jit, 0x0A00);
  uint8_t* p_A01_host_block = jit_get_jit_block_host_address(s_p_jit, 0x0A01);

  jit_get_6502_details_from_host_ip(s_p_jit, &details, p_A00_host_block);
  test_expect_u32(0xFFFFFFFF, details.pc_6502);
  test_expect_u32(0xFFFFFFFF, details.block_6502);
  test_expect_u32(0, (uint32_t) (uintptr_t) details.p_invalidation_code_block);

  util_buffer_setup(p_buf, (s_p_mem + 0xA00), 0x100);
  emit_PLA(p_buf);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0xA00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  jit_get_6502_details_from_host_ip(s_p_jit, &details, p_A00_host_block);
  test_expect_u32(0xFFFFFFFF, details.pc_6502);
  test_expect_u32(0xFFFFFFFF, details.block_6502);
  test_expect_u32((uint32_t) (uintptr_t) p_A00_host_block,
                  (uint32_t) (uintptr_t) details.p_invalidation_code_block);
  jit_get_6502_details_from_host_ip(s_p_jit, &details, p_A01_host_block);
  test_expect_u32(0xFFFFFFFF, details.pc_6502);
  test_expect_u32(0xFFFFFFFF, details.block_6502);
  test_expect_u32((uint32_t) (uintptr_t) p_A00_host_block,
                  (uint32_t) (uintptr_t) details.p_invalidation_code_block);
  p_jit_ptr = jit_get_host_jit_ptr(s_p_jit, 0xA00);
  jit_get_6502_details_from_host_ip(s_p_jit, &details, p_jit_ptr);
  test_expect_u32(1, details.exact_match);
  test_expect_u32(0xA00, details.pc_6502);
  test_expect_u32(0xA00, details.block_6502);
  test_expect_u32((uint32_t) (uintptr_t) p_A00_host_block,
                  (uint32_t) (uintptr_t) details.p_invalidation_code_block);
  p_jit_ptr = jit_get_host_jit_ptr(s_p_jit, 0xA01);
  jit_get_6502_details_from_host_ip(s_p_jit, &details, p_jit_ptr);
  test_expect_u32(1, details.exact_match);
  test_expect_u32(0xA01, details.pc_6502);
  test_expect_u32(0xA00, details.block_6502);
  test_expect_u32((uint32_t) (uintptr_t) p_A00_host_block,
                  (uint32_t) (uintptr_t) details.p_invalidation_code_block);
  p_jit_ptr = jit_get_host_jit_ptr(s_p_jit, 0xA00);
  p_jit_ptr++;
  jit_get_6502_details_from_host_ip(s_p_jit, &details, p_jit_ptr);
  test_expect_u32(0, details.exact_match);
  test_expect_u32(0xA00, details.pc_6502);
  test_expect_u32(0xA00, details.block_6502);
  test_expect_u32((uint32_t) (uintptr_t) p_A00_host_block,
                  (uint32_t) (uintptr_t) details.p_invalidation_code_block);
  p_jit_ptr = jit_get_host_jit_ptr(s_p_jit, 0xA01);
  p_jit_ptr++;
  jit_get_6502_details_from_host_ip(s_p_jit, &details, p_jit_ptr);
  test_expect_u32(0, details.exact_match);
  test_expect_u32(0xA01, details.pc_6502);
  test_expect_u32(0xA00, details.block_6502);
  test_expect_u32((uint32_t) (uintptr_t) p_A00_host_block,
                  (uint32_t) (uintptr_t) details.p_invalidation_code_block);

  /* Emit a ton of non-trivial consecutive 6502 opcodes, such that the 6502
   * code block will spill across multiple host blocks.
   */
  util_buffer_setup(p_buf, (s_p_mem + 0x200), 0x100);
  for (i = 0; i < 200; ++i) {
    emit_PHA(p_buf);
  }
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x200);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  p_jit_ptr = jit_get_host_jit_ptr(s_p_jit, 0x280);
  jit_get_6502_details_from_host_ip(s_p_jit, &details, p_jit_ptr);
  test_expect_u32(0x280, details.pc_6502);
  test_expect_u32(0x200, details.block_6502);
  test_expect_u32((uint32_t) (uintptr_t) p_200_host_block,
                  (uint32_t) (uintptr_t) details.p_invalidation_code_block);

  util_buffer_destroy(p_buf);
}

static void
jit_test_block_split(void) {
  struct util_buffer* p_buf = util_buffer_create();

  jit_test_expect_block_invalidated(1, 0xB00);
  jit_test_expect_block_invalidated(1, 0xB01);

  util_buffer_setup(p_buf, (s_p_mem + 0xB00), 0x100);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0xB00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_block_invalidated(0, 0xB00);
  jit_test_expect_block_invalidated(1, 0xB01);

  state_6502_set_pc(s_p_state_6502, 0xB01);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_block_invalidated(1, 0xB00);
  jit_test_expect_block_invalidated(0, 0xB01);

  state_6502_set_pc(s_p_state_6502, 0xB00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_block_invalidated(0, 0xB00);
  jit_test_expect_block_invalidated(0, 0xB01);

  util_buffer_destroy(p_buf);
}

static void
jit_test_block_continuation(void) {
  struct util_buffer* p_buf = util_buffer_create();

  util_buffer_setup(p_buf, (s_p_mem + 0xC00), 0x100);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  /* Block continuation here because we set the limit to 4 opcodes. */
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_EXIT(p_buf);

  state_6502_set_pc(s_p_state_6502, 0xC00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_block_invalidated(0, 0xC00);
  jit_test_expect_block_invalidated(1, 0xC01);
  jit_test_expect_block_invalidated(0, 0xC04);

  state_6502_set_pc(s_p_state_6502, 0xC01);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_block_invalidated(0, 0xC01);
  jit_test_expect_block_invalidated(1, 0xC04);
  jit_test_expect_block_invalidated(0, 0xC05);

  util_buffer_destroy(p_buf);
}

static void
jit_test_invalidation(void) {
  struct util_buffer* p_buf = util_buffer_create();

  util_buffer_setup(p_buf, (s_p_mem + 0xD00), 0x100);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  /* Block continuation here. */
  emit_NOP(p_buf);
  emit_NOP(p_buf);
  emit_EXIT(p_buf);

  state_6502_set_pc(s_p_state_6502, 0xD00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_invalidate_code_at_address(s_p_jit, 0xD01);

  state_6502_set_pc(s_p_state_6502, 0xD00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* The block split compiling the invalidation at $0D01 invalidates the block
   * at $0D00.
   */
  jit_test_expect_block_invalidated(1, 0xD00);
  jit_test_expect_block_invalidated(0, 0xD01);

  state_6502_set_pc(s_p_state_6502, 0xD00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  jit_test_expect_block_invalidated(0, 0xD00);

  /* This checks that the invalidation in the middle of a block didn't create
   * a new block boundary.
   */
  jit_test_expect_block_invalidated(1, 0xD01);
  jit_test_expect_block_invalidated(0, 0xD04);

  jit_test_invalidate_code_at_address(s_p_jit, 0xD05);

  /* This execution will create a block at 0xD05 because of the invalidation
   * but it should not be a fundamental block boundary. Also, 0xD04 must remain
   * a block continuation and not a fundamental boundary.
   */
  state_6502_set_pc(s_p_state_6502, 0xD00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* Execute again, should settle back to the original block boundaries and
   * continuations.
   */
  state_6502_set_pc(s_p_state_6502, 0xD00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_block_invalidated(0, 0xD00);
  jit_test_expect_block_invalidated(0, 0xD04);
  jit_test_expect_block_invalidated(1, 0xD05);

  /* Check that no block boundaries appeared in incorrect places. */
  state_6502_set_pc(s_p_state_6502, 0xD03);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_block_invalidated(0, 0xD03);
  jit_test_expect_block_invalidated(1, 0xD04);
  jit_test_expect_block_invalidated(1, 0xD05);

  /* Check our invalidated code cleanup function works. */
  jit_test_invalidate_code_at_address(s_p_jit, 0xD04);
  jit_test_expect_code_invalidated(1, 0xD04);
  jit_cleanup_stale_code(s_p_jit);
  jit_test_expect_block_invalidated(1, 0xD03);
  test_expect_eq((uint32_t) (uintptr_t) s_p_jit->p_jit_ptr_no_code,
                 s_p_jit->jit_ptrs[0xD04]);

  util_buffer_destroy(p_buf);
}

static void
jit_test_dynamic_operand(void) {
  struct util_buffer* p_buf = util_buffer_create();

  state_6502_set_x(s_p_state_6502, 0);

  util_buffer_setup(p_buf, (s_p_mem + 0xE00), 0x80);
  emit_LDA(p_buf, k_abx, 0x0E01);
  emit_STA(p_buf, k_abs, 0xF0);
  emit_LDX(p_buf, k_imm, 0x02);
  emit_STX(p_buf, k_abs, 0x0E01);
  emit_EXIT(p_buf);

  state_6502_set_pc(s_p_state_6502, 0xE00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* After the first run through, the LDA $0E01,X will have been self-modified
   * to LDA $0E02,X and currently status will be awaiting compilation.
   */
  jit_test_expect_block_invalidated(0, 0xE00);
  jit_test_expect_code_invalidated(1, 0xE00);
  jit_test_expect_code_invalidated(0, 0xE03);
  jit_test_expect_block_invalidated(1, 0xE01);

  /* This run should trigger a compilation where the optimizer flips the
   * LDA abx operand to a dynamic one.
   * Then, the subsequent self-modification should not trigger an invalidation.
   */
  state_6502_set_pc(s_p_state_6502, 0xE00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_block_invalidated(0, 0xE00);
  jit_test_expect_code_invalidated(0, 0xE00);
  jit_test_expect_code_invalidated(0, 0xE03);
  jit_test_expect_block_invalidated(1, 0xE01);

  jit_test_invalidate_code_at_address(s_p_jit, 0xE01);
  jit_test_invalidate_code_at_address(s_p_jit, 0xE02);

  jit_test_expect_block_invalidated(0, 0xE00);

  /* Put a different opcode, LDA aby, at 0xE00, and recompile. The resulting
   * opcode should not have a dynamic operand right away.
   */
  s_p_mem[0xE00] = 0xB9;
  jit_test_invalidate_code_at_address(s_p_jit, 0xE00);
  state_6502_set_pc(s_p_state_6502, 0xE00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_code_invalidated(1, 0xE00);

  /* Try again but with the dynamic operand opcode not at the block start. */
  util_buffer_setup(p_buf, (s_p_mem + 0xE80), 0x10);
  emit_LDY(p_buf, k_imm, 0x84);
  emit_LDA(p_buf, k_abx, 0x0E83);
  emit_STY(p_buf, k_abs, 0x0E83);
  emit_EXIT(p_buf);

  state_6502_set_pc(s_p_state_6502, 0xE80);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  jit_test_expect_block_invalidated(0, 0xE80);
  jit_test_expect_block_invalidated(1, 0xE82);
  jit_test_expect_code_invalidated(1, 0xE82);

  state_6502_set_pc(s_p_state_6502, 0xE80);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  jit_test_expect_block_invalidated(1, 0xE80);
  jit_test_expect_block_invalidated(0, 0xE82);
  jit_test_expect_code_invalidated(0, 0xE82);

  jit_test_invalidate_code_at_address(s_p_jit, 0xE84);
  jit_test_expect_block_invalidated(0, 0xE82);
  jit_test_expect_code_invalidated(0, 0xE82);

  state_6502_set_pc(s_p_state_6502, 0xE80);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  jit_test_expect_block_invalidated(0, 0xE80);
  jit_test_expect_block_invalidated(1, 0xE82);
  jit_test_expect_code_invalidated(0, 0xE82);

  jit_test_invalidate_code_at_address(s_p_jit, 0xE84);
  jit_test_expect_block_invalidated(1, 0xE82);
  jit_test_expect_code_invalidated(0, 0xE82);

  /* When we do the recompile for the self-modified code, it splits the block
   * starting at 0xE80 and invalidates it.
   * When we recompile that block, make sure we didn't mistake the block split
   * invalidation for a self-modify invalidation, i.e. the code at $0E80 should
   * not have been compiled as dynamic operand.
   */
  jit_test_invalidate_code_at_address(s_p_jit, 0xE81);
  jit_test_expect_code_invalidated(1, 0xE80);

  /* Try again but with the two dynamic operands in a block. */
  util_buffer_setup(p_buf, (s_p_mem + 0xE90), 0x10);
  emit_LDX(p_buf, k_imm, 0x01);
  emit_LDY(p_buf, k_imm, 0x02);
  emit_EXIT(p_buf);

  state_6502_set_pc(s_p_state_6502, 0xE90);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_invalidate_code_at_address(s_p_jit, 0xE91);
  jit_test_invalidate_code_at_address(s_p_jit, 0xE93);

  state_6502_set_pc(s_p_state_6502, 0xE90);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_invalidate_code_at_address(s_p_jit, 0xE91);
  jit_test_invalidate_code_at_address(s_p_jit, 0xE93);

  /* There was a bug where dynamic operands would not be handled later in a
   * block. This is tested here. If the bug triggers, the code at $0E92 would
   * currently be in an invalidated state, resulting in a compile and split
   * for this next execution.
   */
  state_6502_set_pc(s_p_state_6502, 0xE90);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_block_invalidated(0, 0xE90);
  jit_test_expect_block_invalidated(1, 0xE92);

  util_buffer_destroy(p_buf);
}

static void
jit_test_dynamic_operand_2(void) {
  /* Test dynamic operand handling that needs history to work in order to create
   * dynamic operands.
   */
  struct util_buffer* p_buf = util_buffer_create();

  util_buffer_setup(p_buf, (s_p_mem + 0xF00), 0x100);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_EXIT(p_buf);

  state_6502_set_pc(s_p_state_6502, 0xF00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  s_p_mem[0xF01] = 0x02;
  jit_test_invalidate_code_at_address(s_p_jit, 0xF01);
  jit_test_expect_code_invalidated(1, 0xF00);

  /* First compile-time encounter of the self-modified code. */
  state_6502_set_pc(s_p_state_6502, 0xF00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* But it's not enough to create a dynamic operand. */
  s_p_mem[0xF01] = 0x03;
  jit_test_invalidate_code_at_address(s_p_jit, 0xF01);
  jit_test_expect_code_invalidated(1, 0xF00);

  /* Second compile-time encounter of the self-modified code. */
  state_6502_set_pc(s_p_state_6502, 0xF00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* It's enough to create a dynamic operand. */
  s_p_mem[0xF01] = 0x03;
  jit_test_invalidate_code_at_address(s_p_jit, 0xF01);
  jit_test_expect_code_invalidated(0, 0xF00);

  /* Check dynamic operand persists if we compile a block that runs into the
   * dynamic operand.
   */
  s_p_mem[0xEFF] = 0xEA;
  state_6502_set_pc(s_p_state_6502, 0xEFF);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  s_p_mem[0xF01] = 0x04;
  jit_test_invalidate_code_at_address(s_p_jit, 0xF01);
  jit_test_expect_code_invalidated(0, 0xF00);

  util_buffer_destroy(p_buf);
}

static void
jit_test_dynamic_operand_3(void) {
  /* Test dynamic operand handling where it is tricky for the compiler to
   * settle to a stable state.
   */
  uint32_t i;
  struct util_buffer* p_buf = util_buffer_create();

  /* Test 1: The dynamic operand opcode starts off optimized away. */
  s_p_mem[0xF0] = 0x00;
  s_p_mem[0xF1] = 0x10;

  util_buffer_setup(p_buf, (s_p_mem + 0x1000), 0x100);
  emit_LDX(p_buf, k_abs, 0x1000);
  emit_LDY(p_buf, k_imm, 0);      /* Optimizes away. */
  emit_LDA(p_buf, k_idy, 0xF0);
  emit_LDY(p_buf, k_imm, 1);
  emit_INC(p_buf, k_abs, 0x1004); /* Increment operand for LDY imm. */
  emit_EXIT(p_buf);

  for (i = 0; i < 16; ++i) {
    state_6502_set_pc(s_p_state_6502, 0x1000);
    jit_enter(s_p_cpu_driver);
    interp_testing_unexit(s_p_interp);
    if (i == 0) {
      /* We expect the optimzied away opcode to flag as invalidated, if the
       * opcode it was merged into is invalidated.
       */
      test_expect_u32(1, jit_has_invalidated_code(s_p_compiler, 0x1000));
      test_expect_u32(1, jit_has_invalidated_code(s_p_compiler, 0x1003));
    }
  }

  /* The compiler should have been able to settle the above code to dynamic
   * operands, even though the self-modify target is optimized out.
   */
  test_expect_u32(0, jit_has_invalidated_code(s_p_compiler, 0x1000));
  test_expect_u32(0, jit_has_invalidated_code(s_p_compiler, 0x1003));
  /* The compiler should also have been able to only use dynamic operands for
   * the one affected opcode.
   */
  test_expect_u32(0, jit_is_jit_ptr_dyanmic(s_p_jit, 0x1001));
  test_expect_u32(1, jit_is_jit_ptr_dyanmic(s_p_jit, 0x1004));

  /* Test 2: Based on the Exile sprite unpack loop.
   * Within a single block, there are a series of self-modifications that
   * modify the instruction immediately following.
   * This caused a failure to retain dynamic operand status, because the opcodes
   * later in the block get recompiled multiple times -- and they won't appear
   * self-modified for many of the recompiles.
   */
  s_p_mem[0xF0] = 0x00;

  util_buffer_setup(p_buf, (s_p_mem + 0x1100), 0x100);
  emit_LDX(p_buf, k_imm, 16);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_STA(p_buf, k_abs, 0x1108);
  /* 0x1107 */
  emit_LDA(p_buf, k_zpg, 0xFF);
  emit_LDA(p_buf, k_zpg, 0xF0);
  emit_STA(p_buf, k_abs, 0x110F);
  /* 0x110E */
  emit_LDA(p_buf, k_zpg, 0xFF);
  emit_DEX(p_buf);
  emit_BNE(p_buf, -17);
  emit_EXIT(p_buf);

  state_6502_set_pc(s_p_state_6502, 0x1100);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(1, jit_is_jit_ptr_dyanmic(s_p_jit, 0x1108));
  test_expect_u32(1, jit_is_jit_ptr_dyanmic(s_p_jit, 0x110F));
  jit_test_expect_block_invalidated(0, 0x1102);
}

static void
jit_test_dynamic_opcode(void) {
  uint64_t num_compiles;
  uint64_t ticks;
  struct util_buffer* p_buf = util_buffer_create();

  util_buffer_setup(p_buf, (s_p_mem + 0x1900), 0x100);
  emit_LDX(p_buf, k_imm, 0xAA);
  emit_INX(p_buf);
  emit_STX(p_buf, k_zpg, 0x50);
  emit_EXIT(p_buf);

  ticks = timing_get_total_timer_ticks(s_p_timing);
  state_6502_set_pc(s_p_state_6502, 0x1900);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(13, (timing_get_total_timer_ticks(s_p_timing) - ticks));

  /* Invalidate INX once and recompile. */
  s_p_mem[0x1902] = 0xEA;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1902);
  ticks = timing_get_total_timer_ticks(s_p_timing);
  state_6502_set_pc(s_p_state_6502, 0x1900);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(13, (timing_get_total_timer_ticks(s_p_timing) - ticks));
  test_expect_u32(0, jit_is_jit_ptr_dyanmic(s_p_jit, 0x1902));

  /* Replace INX with DEX, should compile as dynamic opcode. */
  s_p_mem[0x1902] = 0xCA;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1902);

  ticks = timing_get_total_timer_ticks(s_p_timing);
  state_6502_set_pc(s_p_state_6502, 0x1900);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(0xA9, s_p_mem[0x50]);
  test_expect_u32(13, (timing_get_total_timer_ticks(s_p_timing) - ticks));
  test_expect_u32(1, jit_is_jit_ptr_dyanmic(s_p_jit, 0x1902));
  /* Dynamic operands always show as invalidated. */
  test_expect_u32(1, jit_has_invalidated_code(s_p_compiler, 0x1902));

  /* Replace DEX with INX, should not invalidate the dynamic opcode. We check
   * by making sure it doesn't incur a recompile.
   */
  s_p_mem[0x1902] = 0xE8;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1902);
  /* Dynamic operands always show as invalidated. */
  test_expect_u32(1, jit_has_invalidated_code(s_p_compiler, 0x1902));

  num_compiles = s_p_jit->counter_num_compiles;
  state_6502_set_pc(s_p_state_6502, 0x1900);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(0xAB, s_p_mem[0x50]);
  test_expect_u32(num_compiles, s_p_jit->counter_num_compiles);

  /* Replace INX with DEX again, but using JIT'ed code. */
  util_buffer_setup(p_buf, (s_p_mem + 0x1980), 0x80);
  emit_LDA(p_buf, k_imm, 0xCA);
  emit_STA(p_buf, k_abs, 0x1902);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x1980);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  test_expect_u32(1, jit_has_invalidated_code(s_p_compiler, 0x1902));

  /* Replace INX with an opcode that needs to cause a self-modified
   * invalidation: STX abs.
   */
  s_p_mem[0x1902] = 0x8E;
  s_p_mem[0x1903] = 0x00;
  s_p_mem[0x1904] = 0x19;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1902);
  jit_test_invalidate_code_at_address(s_p_jit, 0x1903);
  jit_test_invalidate_code_at_address(s_p_jit, 0x1904);

  jit_test_expect_code_invalidated(0, 0x1900);

  state_6502_set_pc(s_p_state_6502, 0x1900);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(0xAA, s_p_mem[0x1900]);
  jit_test_expect_code_invalidated(1, 0x1900);

  /* Check self-modified invalidation via STA idy. */
  state_6502_set_x(s_p_state_6502, 0);
  state_6502_set_y(s_p_state_6502, 0);
  s_p_mem[0x00] = 0x00;
  s_p_mem[0x01] = 0x19;
  s_p_mem[0x1900] = 0xEA;
  s_p_mem[0x1901] = 0xEA;
  s_p_mem[0x1902] = 0x91;
  s_p_mem[0x1903] = 0x00;
  s_p_mem[0x1904] = 0xEA;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1900);
  jit_test_invalidate_code_at_address(s_p_jit, 0x1901);
  jit_test_invalidate_code_at_address(s_p_jit, 0x1902);
  jit_test_invalidate_code_at_address(s_p_jit, 0x1903);
  jit_test_invalidate_code_at_address(s_p_jit, 0x1904);

  state_6502_set_pc(s_p_state_6502, 0x1900);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_code_invalidated(1, 0x1900);

  /* Flip the dynamic opcode to ROL A -- there was a nasty crash bug here due
   * to confusion with ROL rmw vs. ROL A.
   */
  s_p_mem[0x1902] = 0x2A;
  s_p_mem[0x1903] = 0xEA;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1902);
  jit_test_invalidate_code_at_address(s_p_jit, 0x1903);

  state_6502_set_pc(s_p_state_6502, 0x1900);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* Compile a dynamic opcode as the first one in a block. */
  util_buffer_setup(p_buf, (s_p_mem + 0x1A00), 0x100);
  emit_STX(p_buf, k_zpg, 0x50);
  emit_EXIT(p_buf);

  state_6502_set_pc(s_p_state_6502, 0x1A00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* First invalidation. */
  s_p_mem[0x1A00] = 0x85;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1A00);
  state_6502_set_pc(s_p_state_6502, 0x1A00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* Second invalidation, back to STX. */
  s_p_mem[0x1A00] = 0x86;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1A00);
  state_6502_set_pc(s_p_state_6502, 0x1A00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(1, jit_is_jit_ptr_dyanmic(s_p_jit, 0x1A00));

  /* Trigger a block split in the middle of the dynamic opcode! */
  s_p_mem[0x1A01] = 0xEA;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1A01);
  state_6502_set_pc(s_p_state_6502, 0x1A01);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  jit_test_expect_block_invalidated(1, 0x1A00);

  util_buffer_destroy(p_buf);
}

static void
jit_test_dynamic_opcode_2(void) {
  struct util_buffer* p_buf = util_buffer_create();

  /* Trigger what looks like a dynamic operand, but on an opcode for which
   * we don't handle dyamic operands. It should use a dynamic opcode instead.
   */
  util_buffer_setup(p_buf, (s_p_mem + 0x1B00), 0x100);
  emit_BEQ(p_buf, 0);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x1B00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* First invalidation. */
  s_p_mem[0x1B01] = 0x00;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1B01);
  state_6502_set_pc(s_p_state_6502, 0x1B00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(1, jit_is_jit_ptr_dyanmic(s_p_jit, 0x1B00));
  test_expect_u32(1, jit_is_jit_ptr_dyanmic(s_p_jit, 0x1B01));

  util_buffer_destroy(p_buf);
}

static void
jit_test_dynamic_opcode_3(void) {
  uint64_t ticks;
  struct util_buffer* p_buf = util_buffer_create();

  /* Trigger a dynamic opcode that itself bounces inturbo -> interp. We'll use
   * a hardware register read.
   */
  util_buffer_setup(p_buf, (s_p_mem + 0x1C00), 0x100);
  emit_LDA(p_buf, k_abs, 0x1000);
  emit_STA(p_buf, k_zpg, 0x50);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x1C00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* First invalidation. */
  s_p_mem[0x1C00] = 0xAE;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1C00);
  state_6502_set_pc(s_p_state_6502, 0x1C00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  /* Second invalidation. */
  s_p_mem[0x1C00] = 0xAC;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1C00);
  state_6502_set_pc(s_p_state_6502, 0x1C00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(1, jit_is_jit_ptr_dyanmic(s_p_jit, 0x1C00));

  /* Switch to LDA $FE20. */
  s_p_mem[0x1C00] = 0xAD;
  s_p_mem[0x1C01] = 0x20;
  s_p_mem[0x1C02] = 0xFE;
  jit_test_invalidate_code_at_address(s_p_jit, 0x1C00);
  jit_test_invalidate_code_at_address(s_p_jit, 0x1C01);
  jit_test_invalidate_code_at_address(s_p_jit, 0x1C02);
  ticks = timing_get_total_timer_ticks(s_p_timing);
  state_6502_set_pc(s_p_state_6502, 0x1C00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(0xFE, s_p_mem[0x50]);
  test_expect_u32(13, (timing_get_total_timer_ticks(s_p_timing) - ticks));

  util_buffer_destroy(p_buf);
}

static void
jit_test_sub_instruction(void) {
  uint64_t num_compiles;
  struct util_buffer* p_buf = util_buffer_create();

  /* Trigger a recompile situation with sub-instruction jumps. */
  util_buffer_setup(p_buf, (s_p_mem + 0x1D00), 0x100);
  emit_LDA(p_buf, k_imm, 0x04);
  emit_BIT(p_buf, k_abs, 0x00A9);
  emit_EXIT(p_buf);

  state_6502_set_pc(s_p_state_6502, 0x1D00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  state_6502_set_pc(s_p_state_6502, 0x1D03);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  state_6502_set_pc(s_p_state_6502, 0x1D00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  num_compiles = s_p_jit->counter_num_compiles;

  state_6502_set_pc(s_p_state_6502, 0x1D03);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  state_6502_set_pc(s_p_state_6502, 0x1D00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);

  test_expect_u32(num_compiles, s_p_jit->counter_num_compiles);

  util_buffer_destroy(p_buf);
}

static void
jit_test_compile_metadata(void) {
  /* Test metadata correctness, especially in the presence of optimizations. */
  struct util_buffer* p_buf;
  int32_t cycles;
  int32_t a;
  int32_t x;

  /* Test a trivial case. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x1E00), 0x100);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x1E00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x1E00);
  test_expect_u32(6, cycles);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x1E01);
  test_expect_u32(-1, cycles);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x1E02);
  test_expect_u32(4, cycles);
  a = jit_compiler_testing_get_a_fixup(s_p_compiler, 0x1E00);
  test_expect_u32(-1, a);
  util_buffer_destroy(p_buf);

  /* Test a back-merger of a couple of instructions into one. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x1F00), 0x100);
  emit_LSR(p_buf, k_acc, 0);
  emit_LSR(p_buf, k_acc, 0);
  emit_NOP(p_buf);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x1F00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  test_expect_eq(s_p_jit->jit_ptrs[0x1F00], s_p_jit->jit_ptrs[0x1F01]);
  test_expect_neq(s_p_jit->jit_ptrs[0x1F00], s_p_jit->jit_ptrs[0x1F02]);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x1F00);
  test_expect_u32(12, cycles);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x1F01);
  test_expect_u32(-1, cycles);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x1F02);
  test_expect_u32(8, cycles);
  a = jit_compiler_testing_get_a_fixup(s_p_compiler, 0x1F00);
  test_expect_u32(-1, a);
  util_buffer_destroy(p_buf);

  /* Test a forward elimination, one instruction after the block start. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x2000), 0x100);
  emit_CLD(p_buf);
  emit_CLC(p_buf);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x2000);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  test_expect_eq(s_p_jit->jit_ptrs[0x2000], s_p_jit->jit_ptrs[0x2001]);
  test_expect_neq(s_p_jit->jit_ptrs[0x2000], s_p_jit->jit_ptrs[0x2002]);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x2000);
  test_expect_u32(12, cycles);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x2002);
  test_expect_u32(8, cycles);
  util_buffer_destroy(p_buf);

  /* Test a forward elimination, at the block start. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x2100), 0x100);
  emit_LDX(p_buf, k_imm, 0x41);
  emit_JMP(p_buf, k_abs, 0x2105);
  emit_LDX(p_buf, k_imm, 0x42);
  emit_LDX(p_buf, k_imm, 0x43);
  emit_NOP(p_buf);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x2100);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  test_expect_eq(s_p_jit->jit_ptrs[0x2105], s_p_jit->jit_ptrs[0x2106]);
  test_expect_eq(s_p_jit->jit_ptrs[0x2105], s_p_jit->jit_ptrs[0x2107]);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x2105);
  test_expect_u32(12, cycles);
  x = jit_compiler_testing_get_x_fixup(s_p_compiler, 0x2105);
  test_expect_u32(-1, x);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x2106);
  test_expect_u32(-1, cycles);
  x = jit_compiler_testing_get_x_fixup(s_p_compiler, 0x2106);
  test_expect_u32(-1, x);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x2107);
  test_expect_u32(10, cycles);
  x = jit_compiler_testing_get_x_fixup(s_p_compiler, 0x2107);
  test_expect_u32(0x42, x);
  cycles = jit_compiler_testing_get_cycles_fixup(s_p_compiler, 0x2108);
  test_expect_u32(-1, cycles);
  x = jit_compiler_testing_get_x_fixup(s_p_compiler, 0x2108);
  test_expect_u32(-1, x);
  util_buffer_destroy(p_buf);
}

static void
jit_test_compile_binary(void) {
  /* Direct tests on the output binary code.
   * These tests are fragile, but it's worth a few of them to ensure that
   * certain expected optimizations are present in the output. Without these
   * tests, an optimization could fail without us ever noticing.
   */
  struct util_buffer* p_buf;
  void* p_binary;
  void* p_expect = NULL;
  size_t expect_len = 0;

  /* Check NZ flag elimination. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3000), 0x100);
  emit_LDA(p_buf, k_zpg, 0x41);
  emit_ORA(p_buf, k_imm, 0x07);
  emit_LDX(p_buf, k_zpg, 0x41);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3000);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3000);
#if defined(__x86_64__)
  /* movzx  eax, BYTE PTR [rbp-0x3f]
   * or     al,  0x07
   * mov    bl,  BYTE PTR [rbp-0x3f]
   */
  p_expect = "\x0f\xb6\x45\xc1" "\x0c\x07" "\x8a\x5d\xc1";
  expect_len = 9;
#elif defined(__aarch64__)
  /* ldrb  w0, [x27, #0x41]
   * orr   x0, x0, #0x7
   * ldrb  w1, [x27, #0x41]
   */
  p_expect = "\x60\x07\x41\x39" "\x00\x08\x40\xb2" "\x61\x07\x41\x39";
  expect_len = 12;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check CLC/ADC -> ADD. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3100), 0x100);
  emit_CLD(p_buf);
  emit_CLC(p_buf);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3100);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3100);
#if defined(__x86_64__)
  /* btr    r13d, 0x3
   * add    al,  0x1
   */
  p_expect = "\x41\x0f\xba\xf5\x03" "\x04\x01";
  expect_len = 2;
#elif defined(__aarch64__)
  /* and   x5, x5, #0xfffffffffffffff7
   * mov   x20, #0x1000000
   * adds  w0, w20, w0, lsl #24
   */
  p_expect = "\xa5\xf8\x7c\x92" "\x14\x20\xa0\xd2" "\x80\x62\x00\x2b";
  expect_len = 12;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check carry and overflow elimination across 2x ADC. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3200), 0x100);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_ADC(p_buf, k_zpg, 0x42);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3200);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3200);
#if defined(__x86_64__)
  /* mov    r9b, BYTE PTR [r13+0x12017ffa]
   * shr    r14b, 1
   * adc    al, 0x1
   * adc    al, BYTE PTR [rbp-0x3e]
   */
  p_expect = "\x45\x8a\x8d\xfa\x7f\x01\x12"
             "\x41\xd0\xee"
             "\x14\x01"
             "\x12\x45\xc2";
  expect_len = 15;
#elif defined(__aarch64__)
  /* Note that we can only eliminate the overflow and not the carry because of
   * how the ARM64 backend handles carry.
   */
  /* tbnz  w5, #3, 0x300190068
   * mov   x20, #0x1000000
   * add   x20, x20, x6
   * lsl   x0, x0, #24
   * orr   x0, x0, #0xffffff
   * adds  w0, w0, w20
   * lsr   x0, x0, #24
   * cset  x6, cs
   * ldrb  w20, [x27, 0x41]
   */
  p_expect = "\x05\x03\x18\x37" "\x14\x20\xa0\xd2" "\x94\x02\x06\x8b"
             "\x00\x9c\x68\xd3" "\x00\x5c\x40\xb2" "\x00\x00\x14\x2b"
             "\x00\xfc\x58\xd3" "\xe6\x37\x9f\x9a" "\x74\x0b\x41\x39";
  expect_len = 36;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check a simple mode IDY load elimination. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3300), 0x100);
  emit_EOR(p_buf, k_idy, 0x70);
  emit_STA(p_buf, k_idy, 0x70);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3300);
  /* Avoid emitting page crossing check. */
  jit_compiler_testing_set_accurate_cycles(s_p_compiler, 0);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  jit_compiler_testing_set_accurate_cycles(s_p_compiler, 1);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3300);
#if defined(__x86_64__)
  /* movzx  edx, BYTE PTR [rbp-0x10]
   * mov    dh, BYTE PTR [rbp-0x0f]
   * xor    al, BYTE PTR [rdx+rcx*1+0x10008000]
   * mov    BYTE PTR [rdx+rcx*1+0x11008000], al
   */
  p_expect = "\x0f\xb6\x55\xf0"
             "\x8a\x75\xf1"
             "\x32\x84\x0a\x00\x80\x00\x10"
             "\x88\x84\x0a\x00\x80\x00\x11";
  expect_len = 21;
#elif defined(__aarch64__)
  /* TODO: the second add y / addr check can also be eliminated. */
  /* ldrb  w22, [x27, #112]
   * ldrb  w4, [x27, #113]
   * orr   x22, x22, x4, lsl #8
   * add   x21, x22, x2
   * add   x4, x21, #0x400
   * tbnz  w4, #16, 0x300198068
   * ldrb  w20, [x27, x21]
   * eor   x0, x0, x20
   * add   x21, x22, x2
   * add   x4, x21, #0x400
   * tbnz  w4, #16, 0x30019805c
   * strb  w0, [x28, x21]
   */
  p_expect = "\x76\xc3\x41\x39" "\x64\xc7\x41\x39" "\xd6\x22\x04\xaa"
             "\xd5\x02\x02\x8b" "\xa4\x02\x10\x91" "\x64\x02\x80\x37"
             "\x74\x6b\x75\x38" "\x00\x00\x14\xca" "\xd5\x02\x02\x8b"
             "\xa4\x02\x10\x91" "\x64\x01\x80\x37" "\x80\x6b\x35\x38";
  expect_len = 48;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check that known-Y and IDY base load elimination are combining correctly,
   * since this combo appears in BBC BASIC optimizations a lot.
   */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3400), 0x100);
  emit_LDY(p_buf, k_imm, 0x04);
  emit_LDA(p_buf, k_idy, 0x4B);
  emit_STA(p_buf, k_zpg, 0x41);
  emit_DEY(p_buf);
  emit_LDA(p_buf, k_idy, 0x4B);
  emit_STA(p_buf, k_zpg, 0x40);
  emit_DEY(p_buf);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3400);
  /* Avoid emitting page crossing check. */
  jit_compiler_testing_set_accurate_cycles(s_p_compiler, 0);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  jit_compiler_testing_set_accurate_cycles(s_p_compiler, 1);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3400);
#if defined(__x86_64__)
  /* movzx  edx, BYTE PTR [rbp-0x35]
   * mov    dh, BYTE PTR [rbp-0x34]
   * movzx  eax, BYTE PTR [rdx+rbp*1-0x7c]
   * mov    BYTE PTR [rbp-0x3f],al
   * movzx  eax,BYTE PTR [rdx+rbp*1-0x7d]
   * mov    BYTE PTR [rbp-0x40],al
   */
  p_expect = "\x0f\xb6\x55\xcb"
             "\x8a\x75\xcc"
             "\x0f\xb6\x44\x2a\x84"
             "\x88\x45\xc1"
             "\x0f\xb6\x44\x2a\x83"
             "\x88\x45\xc0";
  expect_len = 23;
#elif defined(__aarch64__)
  /* ldrb  w22, [x27, #75]
   * ldrb  w4, [x27, #76]
   * orr   x22, x22, x4, lsl #8
   * add   x21, x22, #0x4
   * add   x4, x21, #0x400
   * tbnz  w4, #16, 0x3001a0068
   * ldrb  w0, [x27, x21]
   * strb  w0, [x28, #65]
   * add   x21, x22, #0x3
   * add   x4, x21, #0x400
   * tbnz  w4, #16, 0x3001a005c
   * ldrb  w0, [x27, x21]
   * strb  w0, [x28, #64]
   */
  p_expect = "\x76\x2f\x41\x39" "\x64\x33\x41\x39" "\xd6\x22\x04\xaa"
             "\xd5\x12\x00\x91" "\xa4\x02\x10\x91" "\x64\x02\x80\x37"
             "\x60\x6b\x75\x38" "\x80\x07\x01\x39" "\xd5\x0e\x00\x91"
             "\xa4\x02\x10\x91" "\x64\x01\x80\x37" "\x60\x6b\x75\x38"
             "\x80\x03\x01\x39";
  expect_len = 52;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check the dynamic operand output for mode ABX, since this occurs a lot
   * in sprite routines.
   */
  jit_compiler_testing_set_dynamic_operand(s_p_compiler, 1);
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3500), 0x100);
  emit_LDA(p_buf, k_abx, 0x2324);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3500);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  /* Invalidate then compile it a second time to get the dynamic operand. */
  jit_test_invalidate_code_at_address(s_p_jit, 0x3501);
  state_6502_set_pc(s_p_state_6502, 0x3500);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  util_buffer_destroy(p_buf);
  jit_compiler_testing_set_dynamic_operand(s_p_compiler, 0);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3500);
#if defined(__x86_64__)
  /* movzx  edx,BYTE PTR [rbp+0x3481]
   * mov    dh,BYTE PTR [rbp+0x3482]
   * movzx  eax,BYTE PTR [rdx+rbx*1+0x10008000]
   */
  p_expect = "\x0f\xb6\x95\x81\x34\x00\x00"
             "\x8a\xb5\x82\x34\x00\x00"
             "\x0f\xb6\x84\x1a\x00\x80\x00\x10";
  expect_len = 21;
#elif defined(__aarch64__)
  /* mov   x21, #0x3501
   * ldrb  w21, [x27, x21]
   * mov   x4, #0x3502
   * ldrb  w4, [x27, x4]
   * orr   x21, x21, x4, lsl #8
   * add   x21, x21, x1
   * add   x4, x21, #0x400
   * tbnz  w4, #16, 0x3001a8068
   * ldrb  w0, [x27, x21]
   */
  p_expect = "\x35\xa0\x86\xd2" "\x75\x6b\x75\x38" "\x44\xa0\x86\xd2"
             "\x64\x6b\x64\x38" "\xb5\x22\x04\xaa" "\xb5\x02\x01\x8b"
             "\xa4\x02\x10\x91" "\x24\x02\x80\x37" "\x60\x6b\x75\x38";
  expect_len = 36;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check the output for ROL zpg. It's a performance hot spot in a BASIC math
   * routine.
   */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3600), 0x100);
  emit_ROL(p_buf, k_zpg, 0x32);
  emit_ROL(p_buf, k_zpg, 0x31);
  emit_DEX(p_buf);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3600);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3600);
#if defined(__x86_64__)
  /* shr    r14b, 1
   * rcl    BYTE PTR [rbp-0x4e], 1
   * rcl    BYTE PTR [rbp-0x4f], 1
   * setb   r14b
   * dec    bl
   */
  p_expect = "\x41\xd0\xee"
             "\xd0\x55\xb2"
             "\xd0\x55\xb1"
             "\x41\x0f\x92\xc6"
             "\xfe\xcb";
  expect_len = 15;
#elif defined(__aarch64__)
  /* ldrb  w20, [x27, #0x32]
   * mov   x4, x6
   * ubfx  x6, x20, #7, #1
   * lsl   x20, x20, #1
   * orr   x20, x20, x4
   * strb  w20, [x28, #0x32]
   * ldrb  w20, [x27, #0x31]
   * mov   x4, x6
   * ubfx  x6, x20, #7, #1
   * lsl   x20, x20, #1
   * orr   x20, x20, x4
   * strb  w20, [x28, #0x31]
   * sub   x1, x1, #0x1
   * and   x1, x1, #0xff
   */
  p_expect = "\x74\xcb\x40\x39" "\xe4\x03\x06\xaa" "\x86\x1e\x47\xd3"
             "\x94\xfa\x7f\xd3" "\x94\x02\x04\xaa" "\x94\xcb\x00\x39"
             "\x74\xc7\x40\x39" "\xe4\x03\x06\xaa" "\x86\x1e\x47\xd3"
             "\x94\xfa\x7f\xd3" "\x94\x02\x04\xaa" "\x94\xc7\x00\x39"
             "\x21\x04\x00\xd1" "\x21\x1c\x40\x92";
  expect_len = 56;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check the output for an SBC chain rolling into an ADC.
   * Exile has similar sequences where the carry flag does a bit of a journey!
   */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3700), 0x100);
  emit_SEC(p_buf);
  emit_SBC(p_buf, k_zpg, 0x40);
  emit_SBC(p_buf, k_zpg, 0x41);
  emit_ADC(p_buf, k_zpg, 0x42);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3700);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3700);
#if defined(__x86_64__)
  /* mov    r9b, BYTE PTR [r13+0x12017ffa]
   * sub    al, BYTE PTR [rbp-0x40]
   * sbb    al, BYTE PTR [rbp-0x3f]
   * cmc
   * adc    al,BYTE PTR [rbp-0x3e]
   */
  p_expect = "\x45\x8a\x8d\xfa\x7f\x01\x12"
             "\x2a\x45\xc0"
             "\x1a\x45\xc1"
             "\xf5"
             "\x12\x45\xc2";
  expect_len = 17;
#elif defined(__aarch64__)
  /* tbnz  w5, #3, 0x3001b8068
   * ldrb  w20, [x27, #64]
   * lsl   x0, x0, #24
   * subs  w0, w0, w20, lsl #24
   * lsr   x0, x0, #24
   * cset  x6, cs
   * ldrb  w20, [x27, #65]
   * [...]
   */
  p_expect = "\x05\x03\x18\x37" "\x74\x03\x41\x39" "\x00\x9c\x68\xd3"
             "\x00\x60\x14\x6b" "\x00\xfc\x58\xd3" "\xe6\x37\x9f\x9a"
             "\x74\x07\x41\x39";
  expect_len = 28;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check the output for LDA #0 where flags are needed. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3800), 0x100);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JMP(p_buf, k_abs, 0x3804);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3800);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3800);
#if defined(__x86_64__)
  /* xor    eax, eax */
  p_expect = "\x31\xc0";
  expect_len = 2;
#elif defined(__aarch64__)
  /* subs  x0, x0, x0 */
  p_expect = "\x00\x00\x00\xeb";
  expect_len = 4;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check the output for LDA imm / STA zpg / LDA imm, which some backends can
   * optimize via the k_opcode_ST_IMM uopcode.
   */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3900), 0x100);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0xE0);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3900);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3900);
#if defined(__x86_64__)
  /* mov    BYTE PTR [rbp+0x60], 0x0 */
  p_expect = "\xc6\x45\x60\x00";
  expect_len = 2;
#elif defined(__aarch64__)
  /* mov   x0, #0x0
   * strb  w0, [x28, #224]
   */
  p_expect = "\x00\x00\x80\xd2" "\x80\x83\x03\x39";
  expect_len = 8;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check for branch cycles fixup merging with countdowns. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3A00), 0x100);
  emit_BEQ(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0x3A05);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3A00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3A00);
#if defined(__x86_64__)
  /* je     0x61d0180
   * lea    r15, [r15 - 2]
   */
  p_expect = "\x0f\x84\x6f\x01\x00\x00" "\x4d\x8d\x7f\xfe";
  expect_len = 10;
#elif defined(__aarch64__)
  /* b.eq  0x61d0180
   * sub   x24, x24, #0x2
   */
  p_expect = "\xc0\x0b\x00\x54" "\x18\x0b\x00\xd1";
  expect_len = 8;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);

  /* Check for flags elimination rolling into a CMP, and a CMP + BCC. */
  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, (s_p_mem + 0x3B00), 0x100);
  emit_LDA(p_buf, k_zpg, 0x45);
  emit_CMP(p_buf, k_imm, 0x96);
  emit_BCC(p_buf, 1);
  emit_INX(p_buf);
  emit_EXIT(p_buf);
  state_6502_set_pc(s_p_state_6502, 0x3B00);
  jit_enter(s_p_cpu_driver);
  interp_testing_unexit(s_p_interp);
  util_buffer_destroy(p_buf);
  p_binary = jit_get_host_jit_ptr(s_p_jit, 0x3B00);
#if defined(__x86_64__)
  /* movzx  eax, BYTE PTR [rbp-0x3b]
   * cmp    al, 0x96
   * setae  r14b
   * jb     0x61d8380
   */
  p_expect = "\x0f\xb6\x45\xc5" "\x3c\x96" "\x41\x0f\x93\xc6"
             "\x0f\x82\x65\x03\x00\x00";
  expect_len = 16;
#elif defined(__aarch64__)
  /* ldrb  w0, [x27, #69]
   * subs  x20, x0, #0x96
   * cset  x6, cs
   * cmn   xzr, x20, lsl #56
   * cbz   x6, 0x61d8380
   */
  p_expect = "\x60\x17\x41\x39" "\x14\x58\x02\xf1" "\xe6\x37\x9f\x9a"
             "\xff\xe3\x14\xab" "\x46\x1b\x00\xb4";
  expect_len = 20;
#endif
  test_expect_binary(p_expect, p_binary, expect_len);
}

void
jit_test(struct bbc_struct* p_bbc) {
  jit_test_init(p_bbc);

  /* Test this with a blank JIT space. */
  jit_cleanup_stale_code(s_p_jit);

  jit_compiler_testing_set_max_ops(s_p_compiler, 1024);
  jit_test_details_from_host_ip();
  jit_compiler_testing_set_max_ops(s_p_compiler, 4);

  jit_test_block_split();
  jit_test_block_continuation();
  jit_test_invalidation();

  jit_compiler_testing_set_dynamic_operand(s_p_compiler, 1);
  jit_test_dynamic_operand();
  jit_compiler_testing_set_dynamic_trigger(s_p_compiler, 2);
  jit_test_dynamic_operand_2();
  jit_compiler_testing_set_dynamic_trigger(s_p_compiler, 4);
  jit_compiler_testing_set_optimizing(s_p_compiler, 1);
  jit_compiler_testing_set_max_ops(s_p_compiler, 1024);
  jit_test_dynamic_operand_3();
  jit_compiler_testing_set_dynamic_trigger(s_p_compiler, 1);
  jit_compiler_testing_set_dynamic_operand(s_p_compiler, 0);
  jit_compiler_testing_set_optimizing(s_p_compiler, 0);
  jit_compiler_testing_set_max_ops(s_p_compiler, 4);

  jit_compiler_testing_set_dynamic_opcode(s_p_compiler, 1);
  jit_test_dynamic_opcode();
  jit_compiler_testing_set_dynamic_opcode(s_p_compiler, 0);

  jit_compiler_testing_set_dynamic_opcode(s_p_compiler, 1);
  jit_compiler_testing_set_dynamic_operand(s_p_compiler, 1);
  jit_test_dynamic_opcode_2();
  jit_test_dynamic_opcode_3();
  jit_compiler_testing_set_dynamic_opcode(s_p_compiler, 0);
  jit_compiler_testing_set_dynamic_operand(s_p_compiler, 0);

  jit_compiler_testing_set_sub_instruction(s_p_compiler, 1);
  jit_test_sub_instruction();
  jit_compiler_testing_set_sub_instruction(s_p_compiler, 0);

  jit_compiler_testing_set_max_ops(s_p_compiler, 1024);
  jit_compiler_testing_set_optimizing(s_p_compiler, 1);
  jit_test_compile_binary();
  jit_test_compile_metadata();
  jit_compiler_testing_set_max_ops(s_p_compiler, 4);
  jit_compiler_testing_set_optimizing(s_p_compiler, 0);

  /* Test this with a JIT space that's been used by all the above tests. */
  jit_cleanup_stale_code(s_p_jit);
}
