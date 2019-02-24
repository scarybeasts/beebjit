#include "asm_x64_abi.h"

#include "asm_x64_common.h"
#include "bbc_options.h"
#include "defs_6502.h"
#include "state_6502.h"

#include <string.h>

void asm_x64_abi_init(struct asm_x64_abi* p_abi,
                      uint8_t* p_memory_read,
                      struct bbc_options* p_options,
                      struct state_6502* p_state_6502) {
  uint32_t uint_mem_read;

  (void) memset(p_abi, '\0', sizeof(struct asm_x64_abi));

  p_abi->p_util_debug = asm_x64_asm_debug;

  p_abi->p_state_6502 = p_state_6502;

  p_abi->p_debug_callback = p_options->debug_callback;
  p_abi->p_debug_object = p_options->p_debug_callback_object;

  uint_mem_read = (uint32_t) (uintptr_t) p_memory_read;
  p_state_6502->reg_x = uint_mem_read;
  p_state_6502->reg_y = uint_mem_read;
  p_state_6502->reg_s = (uint_mem_read + k_6502_stack_addr);
  p_state_6502->reg_pc = uint_mem_read;
}
