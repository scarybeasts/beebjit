#include "test.h"

#include "bbc.h"
#include "jit.h"

#include <assert.h>

static unsigned char*
test_6502_addr_to_intel(uint16_t addr_6502) {
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

  callback(p_jit, test_6502_addr_to_intel(0x1000));

  assert(jit_has_code(p_jit, 0x1000));
  assert(jit_has_code(p_jit, 0x1001));
  assert(jit_has_code(p_jit, 0x1002));
  assert(!jit_has_code(p_jit, 0x1003));

  assert(jit_is_block_start(p_jit, 0x1000));
  assert(!jit_is_block_start(p_jit, 0x1001));
  assert(!jit_is_block_start(p_jit, 0x1002));
  assert(!jit_is_block_start(p_jit, 0x1003));

  /* Split the existing block. */
  callback(p_jit, test_6502_addr_to_intel(0x1002));

  assert(jit_is_block_start(p_jit, 0x1000));
  assert(!jit_is_block_start(p_jit, 0x1001));
  assert(jit_is_block_start(p_jit, 0x1002));
  assert(!jit_is_block_start(p_jit, 0x1003));

  /* Recompile start of fractured block. */
  callback(p_jit, test_6502_addr_to_intel(0x1000));

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
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xee; /* INC $2001 */ /* Self-modifying. */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x20;
  p_mem[index++] = 0x60; /* RTS */

  callback(p_jit, test_6502_addr_to_intel(0x2000));

  /* Simulate the memory effects of running at $2000. */
  p_mem[0x2001] = 0x01;
  jit_memory_written(p_jit, 0x2001);
}

void
test_do_tests(struct bbc_struct* p_bbc) {
  do_basic_jit_tests(p_bbc);
  do_totally_lit_jit_tests(p_bbc);
}
