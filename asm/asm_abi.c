#include "asm_abi.h"

#include "asm_tables.h"
#include "../asm_x64_common.h"
#include "../asm_x64_defs.h"
#include "../bbc_options.h"
#include "../defs_6502.h"
#include "../memory_access.h"
#include "../state_6502.h"

#include <assert.h>
#include <string.h>

void asm_abi_init(struct asm_abi* p_abi,
                  struct memory_access* p_memory_access,
                  struct bbc_options* p_options,
                  struct state_6502* p_state_6502) {
  (void) p_memory_access;

  (void) memset(p_abi, '\0', sizeof(struct asm_abi));

  p_abi->p_state_6502 = p_state_6502;

  p_abi->p_debug_callback = p_options->debug_callback;
  p_abi->p_debug_object = p_options->p_debug_object;

  p_state_6502->reg_a = 0;
  p_state_6502->reg_x = 0;
  p_state_6502->reg_y = 0;
  p_state_6502->reg_s = k_6502_stack_addr;
  p_state_6502->reg_pc = 0;

  asm_tables_init();
}
