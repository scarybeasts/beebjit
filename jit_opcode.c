#include "jit_opcode.h"

#include <assert.h>
#include <stddef.h>

struct jit_uop* jit_opcode_find_uop(struct jit_opcode_details* p_opcode,
                                    int32_t uopcode) {
  uint32_t i_uops;
  struct jit_uop* p_ret = NULL;

  for (i_uops = 0; i_uops < p_opcode->num_uops; ++i_uops) {
    struct jit_uop* p_uop = &p_opcode->uops[i_uops];
    if (p_uop->uopcode == uopcode) {
      assert(p_ret == NULL);
      p_ret = p_uop;
    }
  }

  return p_ret;
}
