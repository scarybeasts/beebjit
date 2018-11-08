#include "asm_x64_abi.h"

#include "asm_x64_common.h"
#include "bbc_options.h"

#include <string.h>

void asm_x64_abi_init(struct asm_x64_abi* p_abi,
                      struct bbc_options* p_options,
                      struct state_6502* p_state_6502) {
  (void) memset(p_abi, '\0', sizeof(struct asm_x64_abi));

  p_abi->p_util_debug = asm_x64_asm_debug;

  p_abi->p_state_6502 = p_state_6502;

  p_abi->p_debug_callback = p_options->debug_callback;
  p_abi->p_debug_object = p_options->p_debug_callback_object;
}
