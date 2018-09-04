#include "test.h"

#include "bbc.h"
#include "jit.h"

#include <assert.h>

static unsigned char*
test_6502_jump_addr_to_intel(uint16_t addr_6502) {
  size_t addr_intel = 0x20000000;
  addr_intel += (addr_6502 << 8);
  addr_intel += 2;

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

  callback(p_jit, test_6502_jump_addr_to_intel(0x1000));

  assert(jit_has_code(p_jit, 0x1000));
  assert(jit_has_code(p_jit, 0x1001));
  assert(jit_has_code(p_jit, 0x1002));
  assert(!jit_has_code(p_jit, 0x1003));

  assert(jit_is_block_start(p_jit, 0x1000));
  assert(!jit_is_block_start(p_jit, 0x1001));
  assert(!jit_is_block_start(p_jit, 0x1002));
  assert(!jit_is_block_start(p_jit, 0x1003));

  /* Split the existing block. */
  callback(p_jit, test_6502_jump_addr_to_intel(0x1002));

  assert(jit_is_block_start(p_jit, 0x1000));
  assert(!jit_is_block_start(p_jit, 0x1001));
  assert(jit_is_block_start(p_jit, 0x1002));
  assert(!jit_is_block_start(p_jit, 0x1003));

  /* Recompile start of fractured block. */
  callback(p_jit, test_6502_jump_addr_to_intel(0x1000));

  assert(jit_is_block_start(p_jit, 0x1000));
  assert(!jit_is_block_start(p_jit, 0x1001));
  assert(jit_is_block_start(p_jit, 0x1002));
  assert(!jit_is_block_start(p_jit, 0x1003));
}

static void
do_totally_lit_jit_tests(struct bbc_struct* p_bbc) {
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
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x20;
  p_mem[index++] = 0x60; /* RTS */

  callback(p_jit, test_6502_jump_addr_to_intel(0x2000));

  assert(jit_is_block_start(p_jit, 0x2000));
  assert(!jit_is_block_start(p_jit, 0x2001));
  assert(!jit_is_block_start(p_jit, 0x2002));

  /* Simulate the memory effects of running at $2000. */
  p_mem[0x2002] = 0x01;
  jit_memory_written(p_jit, 0x2002);

  assert(jit_has_invalidated_code(p_jit, 0x2001));

  /* Run / compile the invalidated opcode. */
  callback(p_jit, jit_get_code_ptr(p_jit, 0x2001) + 2);

  assert(jit_is_block_start(p_jit, 0x2000));
  assert(!jit_is_block_start(p_jit, 0x2001));
  assert(!jit_is_block_start(p_jit, 0x2002));

  /* This effectively split the block, so the block start should be
   * invalidated.
   */
  assert(jit_has_invalidated_code(p_jit, 0x2000));

  /* Executing at $2000 again will therefore recompile the block. */
  assert(jit_get_code_ptr(p_jit, 0x2000) ==
         test_6502_jump_addr_to_intel(0x2000) - 2);

  callback(p_jit, test_6502_jump_addr_to_intel(0x2000));

  assert(jit_is_block_start(p_jit, 0x2000));
  assert(!jit_is_block_start(p_jit, 0x2001));
  assert(!jit_is_block_start(p_jit, 0x2002));

  /* Simulate the memory effects of running at $2000. */
  p_mem[0x2002] = 0x02;
  jit_memory_written(p_jit, 0x2002);

  /* Our JIT shouldn't have invalidated because the LDA #$?? operand is now
   * marked as self-modifying and fetched dynamically.
   */
  assert(!jit_has_invalidated_code(p_jit, 0x2001));
}

void
test_do_tests(struct bbc_struct* p_bbc) {
  do_basic_jit_tests(p_bbc);
  do_totally_lit_jit_tests(p_bbc);
}
