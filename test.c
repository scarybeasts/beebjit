#include "test.h"

#include "bbc.h"
#include "jit.h"

#include <assert.h>

static unsigned char*
test_6502_jump_addr_to_intel(uint16_t addr_6502) {
  size_t addr_intel = 0x20000000;
  addr_intel += (addr_6502 << 8);

  return (unsigned char*) addr_intel;
}

static void
do_basic_jit_tests(struct bbc_struct* p_bbc) {
  size_t index;

  unsigned char* p_mem = bbc_get_mem(p_bbc);
  struct jit_struct* p_jit = bbc_get_jit(p_bbc);
  void (*callback)(struct jit_struct*, unsigned char*) =
      jit_get_jit_callback_for_testing();

  assert(!jit_has_code(p_jit, 0x1000));
  assert(!jit_is_block_start(p_jit, 0x1000));

  index = 0x1000;
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x60; /* RTS */

  callback(p_jit, test_6502_jump_addr_to_intel(0x1000) + 2);

  assert(jit_has_code(p_jit, 0x1000));
  assert(jit_has_code(p_jit, 0x1001));
  assert(jit_has_code(p_jit, 0x1002));
  assert(!jit_has_code(p_jit, 0x1003));
  assert(!jit_has_self_modify_optimize(p_jit, 0x1000));

  assert(jit_is_block_start(p_jit, 0x1000));
  assert(!jit_is_block_start(p_jit, 0x1001));
  assert(!jit_is_block_start(p_jit, 0x1002));
  assert(!jit_is_block_start(p_jit, 0x1003));

  /* Split the existing block. */
  callback(p_jit, test_6502_jump_addr_to_intel(0x1002) + 2);

  assert(jit_is_block_start(p_jit, 0x1000));
  assert(!jit_is_block_start(p_jit, 0x1001));
  assert(jit_is_block_start(p_jit, 0x1002));
  assert(!jit_is_block_start(p_jit, 0x1003));
  assert(!jit_has_self_modify_optimize(p_jit, 0x1000));

  /* Recompile start of fractured block. */
  callback(p_jit, test_6502_jump_addr_to_intel(0x1000) + 2);

  assert(jit_is_block_start(p_jit, 0x1000));
  assert(!jit_is_block_start(p_jit, 0x1001));
  assert(jit_is_block_start(p_jit, 0x1002));
  assert(!jit_is_block_start(p_jit, 0x1003));
  assert(!jit_has_self_modify_optimize(p_jit, 0x1000));
}

static void
do_totally_lit_jit_test_1(struct bbc_struct* p_bbc) {
  size_t index;

  unsigned char* p_mem = bbc_get_mem(p_bbc);
  struct jit_struct* p_jit = bbc_get_jit(p_bbc);
  void (*callback)(struct jit_struct*, unsigned char*) =
      jit_get_jit_callback_for_testing();

  index = 0x2000;
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xee; /* INC $2002 */ /* Self-modifying. */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0x20;
  p_mem[index++] = 0x02; /* Exit JIT. */

  callback(p_jit, test_6502_jump_addr_to_intel(0x2000) + 2);

  assert(jit_is_block_start(p_jit, 0x2000));
  assert(!jit_is_block_start(p_jit, 0x2001));
  assert(!jit_is_block_start(p_jit, 0x2002));

  /* Run at $2000. */
  jit_set_registers(p_jit, 0, 0, 0, 0, 0, 0x2000);
  jit_enter(p_jit);
  assert(p_mem[0x2002] == 0x01);

  assert(jit_has_invalidated_code(p_jit, 0x2001));
  assert(jit_is_block_start(p_jit, 0x2000));
  assert(!jit_is_block_start(p_jit, 0x2001));
  assert(!jit_is_block_start(p_jit, 0x2002));
  assert(!jit_has_self_modify_optimize(p_jit, 0x2001));

  /* Run / compile the invalidated opcode. */
  jit_set_registers(p_jit, 0, 0, 0, 0, 0, 0x2000);
  jit_enter(p_jit);
  assert(p_mem[0x2002] == 0x02);

  assert(jit_is_block_start(p_jit, 0x2000));
  assert(!jit_is_block_start(p_jit, 0x2001));
  assert(!jit_is_block_start(p_jit, 0x2002));
  assert(jit_has_self_modify_optimize(p_jit, 0x2001));

  /* This effectively split the block, so the block start should be
   * invalidated.
   */
  assert(jit_jump_target_is_invalidated(p_jit, 0x2000));
  assert(!jit_has_self_modify_optimize(p_jit, 0x2000));

  /* Executing at $2000 again will therefore recompile the block. */
  jit_set_registers(p_jit, 0, 0, 0, 0, 0, 0x2000);
  jit_enter(p_jit);
  assert(p_mem[0x2002] == 0x03);

  assert(jit_is_block_start(p_jit, 0x2000));
  assert(!jit_is_block_start(p_jit, 0x2001));
  assert(!jit_is_block_start(p_jit, 0x2002));
  assert(jit_has_self_modify_optimize(p_jit, 0x2001));

  /* Our JIT shouldn't have invalidated because the LDA #$?? operand is now
   * marked as self-modifying and fetched dynamically.
   */
  assert(!jit_has_invalidated_code(p_jit, 0x2001));

  /* Clobber the LDA with another LDA. */
  p_mem[0x2001] = 0xa9;
  jit_memory_written(p_jit, 0x2001);
  assert(jit_has_invalidated_code(p_jit, 0x2001));

  callback(p_jit, jit_get_code_ptr(p_jit, 0x2001) + 2);

  assert(jit_has_self_modify_optimize(p_jit, 0x2001));

  /* Clobber the LDA with unrelated opcode ORA #$?? */
  p_mem[0x2001] = 0x09;
  jit_memory_written(p_jit, 0x2001);
  assert(jit_has_invalidated_code(p_jit, 0x2001));

  callback(p_jit, jit_get_code_ptr(p_jit, 0x2001) + 2);

  assert(!jit_has_self_modify_optimize(p_jit, 0x2001));
}

static void
do_totally_lit_jit_test_2(struct bbc_struct* p_bbc) {
  size_t index;

  unsigned char* p_mem = bbc_get_mem(p_bbc);
  struct jit_struct* p_jit = bbc_get_jit(p_bbc);

  index = 0x2100;
  p_mem[index++] = 0xa9; /* LDA #$06 */
  p_mem[index++] = 0x06;
  p_mem[index++] = 0x0a; /* ASL A */
  p_mem[index++] = 0x0a; /* ASL A */
  p_mem[index++] = 0x0a; /* ASL A */
  p_mem[index++] = 0x0a; /* ASL A */
  p_mem[index++] = 0x85; /* STA $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x02; /* Exit JIT. */

  /* Run at $2100. */
  jit_set_registers(p_jit, 0, 0, 0, 0, 0, 0x2100);
  jit_enter(p_jit);
  assert(p_mem[0x0000] == 0x60);

  assert(jit_is_block_start(p_jit, 0x2100));
  assert(!jit_is_block_start(p_jit, 0x2102));
  assert(!jit_is_block_start(p_jit, 0x2103));
  assert(!jit_has_self_modify_optimize(p_jit, 0x2100));
  assert(!jit_has_self_modify_optimize(p_jit, 0x2102));
  assert(!jit_has_invalidated_code(p_jit, 0x2100));
  assert(!jit_has_invalidated_code(p_jit, 0x2102));
  assert(!jit_has_invalidated_code(p_jit, 0x2103));
  assert(!jit_has_invalidated_code(p_jit, 0x2104));
  assert(!jit_has_invalidated_code(p_jit, 0x2105));
  assert(!jit_is_force_invalidated(p_jit, 0x2102));
  assert(jit_is_force_invalidated(p_jit, 0x2103));
  assert(jit_is_force_invalidated(p_jit, 0x2104));
  assert(jit_is_force_invalidated(p_jit, 0x2105));
  assert(!jit_is_force_invalidated(p_jit, 0x2106));
  assert(jit_is_force_invalidated(p_jit, 0x2107));

  /* The ASL A's will have been coalesced into one x64 instruction. Jump into
   * the middle of them.
   */
  /* Run at $2103. */
  jit_set_registers(p_jit, 0x06, 0, 0, 0, 0, 0x2103);
  jit_enter(p_jit);
  assert(p_mem[0x0000] == 0x30);

  assert(jit_is_block_start(p_jit, 0x2100));
  assert(!jit_is_block_start(p_jit, 0x2102));
  assert(jit_is_block_start(p_jit, 0x2103));
  assert(!jit_is_block_start(p_jit, 0x2104));
  assert(jit_jump_target_is_invalidated(p_jit, 0x2100));
  assert(!jit_has_invalidated_code(p_jit, 0x2102));
  assert(!jit_has_invalidated_code(p_jit, 0x2103));
  assert(!jit_has_invalidated_code(p_jit, 0x2104));
  assert(jit_is_force_invalidated(p_jit, 0x2100));
  assert(!jit_is_force_invalidated(p_jit, 0x2102));
  assert(!jit_is_force_invalidated(p_jit, 0x2103));
  assert(jit_is_force_invalidated(p_jit, 0x2104));

  /* Run at $2100 again. */
  jit_set_registers(p_jit, 0, 0, 0, 0, 0, 0x2100);
  jit_enter(p_jit);
  assert(p_mem[0x0000] == 0x60);

  assert(jit_is_block_start(p_jit, 0x2100));
  assert(!jit_is_block_start(p_jit, 0x2102));
  assert(jit_is_block_start(p_jit, 0x2103));
  assert(!jit_is_force_invalidated(p_jit, 0x2100));
  assert(jit_is_force_invalidated(p_jit, 0x2101));
  assert(!jit_is_force_invalidated(p_jit, 0x2102));
  assert(!jit_is_force_invalidated(p_jit, 0x2103));

  /* Clobber an ASL A with a NOP. */
  p_mem[0x2103] = 0xea;
  jit_memory_written(p_jit, 0x2103);
  /* Run at $2103 again. */
  jit_set_registers(p_jit, 0x06, 0, 0, 0, 0, 0x2103);
  jit_enter(p_jit);
  assert(p_mem[0x0000] == 0x18);
}

static void
do_totally_lit_jit_test_3(struct bbc_struct* p_bbc) {
  size_t index;

  unsigned char* p_mem = bbc_get_mem(p_bbc);
  struct jit_struct* p_jit = bbc_get_jit(p_bbc);

  index = 0x2200;
  p_mem[index++] = 0xa9; /* LDA #$CE */
  p_mem[index++] = 0xce;
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x95; /* STA $00,X */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0x02; /* Exit JIT. */

  /* Setting this flag makes us compile only one 6502 instruction and then jump
   * to the next 6502 address.
   */
  jit_clear_flag(p_jit, k_jit_flag_batch_ops);

  /* Run at $2200. */
  jit_set_registers(p_jit, 0, 0, 0, 0, 0, 0x2200);
  jit_enter(p_jit);
  assert(p_mem[0x0000] == 0xce);

  jit_set_flag(p_jit, k_jit_flag_batch_ops);

  assert(jit_is_block_start(p_jit, 0x2200));
  assert(!jit_is_block_start(p_jit, 0x2201));
  assert(!jit_is_block_start(p_jit, 0x2202));
  assert(!jit_is_block_start(p_jit, 0x2203));
  assert(!jit_is_block_start(p_jit, 0x2204));
  assert(!jit_is_block_start(p_jit, 0x2205));
  assert(!jit_is_block_start(p_jit, 0x2206));

  /* Invalidate $2200 to force recompilation. */
  jit_memory_written(p_jit, 0x2200);

  /* Run at $2200. */
  jit_set_registers(p_jit, 0, 0, 0, 0, 0, 0x2200);
  jit_enter(p_jit);

  assert(jit_is_block_start(p_jit, 0x2200));
  assert(!jit_is_block_start(p_jit, 0x2201));
  assert(!jit_is_block_start(p_jit, 0x2202));
  assert(!jit_is_block_start(p_jit, 0x2203));
  assert(!jit_is_block_start(p_jit, 0x2204));
  assert(!jit_is_block_start(p_jit, 0x2205));
  assert(!jit_is_block_start(p_jit, 0x2206));

  /* Run at $2204. */
  jit_set_registers(p_jit, 0xce, 1, 0, 0, 0, 0x2204);
  jit_enter(p_jit);
  assert(p_mem[0x0001] == 0xce);

  assert(jit_is_block_start(p_jit, 0x2200));
  assert(!jit_is_block_start(p_jit, 0x2201));
  assert(!jit_is_block_start(p_jit, 0x2202));
  assert(!jit_is_block_start(p_jit, 0x2203));
  assert(jit_is_block_start(p_jit, 0x2204));
  assert(!jit_is_block_start(p_jit, 0x2205));
  assert(!jit_is_block_start(p_jit, 0x2206));
}

void
test_do_tests(struct bbc_struct* p_bbc) {
  do_basic_jit_tests(p_bbc);
  do_totally_lit_jit_test_1(p_bbc);
  do_totally_lit_jit_test_2(p_bbc);
  do_totally_lit_jit_test_3(p_bbc);
}
