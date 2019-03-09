#include "asm_x64_abi.h"

#include "asm_tables.h"
#include "asm_x64_common.h"
#include "bbc_options.h"
#include "defs_6502.h"
#include "memory_access.h"
#include "state_6502.h"

#include <assert.h>
#include <string.h>

void asm_x64_abi_init(struct asm_x64_abi* p_abi,
                      struct memory_access* p_memory_access,
                      struct bbc_options* p_options,
                      struct state_6502* p_state_6502) {
  uint32_t uint_mem_read;

  uint8_t* p_mem_read = p_memory_access->p_mem_read;

  /* The memory must be aligned to at least 0x10000 so that our register access
   * tricks work.
   */
  assert(((size_t) p_mem_read & 0xffff) == 0);

  (void) memset(p_abi, '\0', sizeof(struct asm_x64_abi));

  p_abi->p_state_6502 = p_state_6502;

  p_abi->p_debug_callback = p_options->debug_callback;
  p_abi->p_debug_object = p_options->p_debug_callback_object;

  uint_mem_read = (uint32_t) (uintptr_t) p_mem_read;
  p_state_6502->reg_x = uint_mem_read;
  p_state_6502->reg_y = uint_mem_read;
  p_state_6502->reg_s = (uint_mem_read + k_6502_stack_addr);
  p_state_6502->reg_pc = uint_mem_read;

  asm_tables_init();
}
