#include "jit_opcode.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

void
jit_opcode_make_internal_opcode1(struct jit_opcode_details* p_opcode,
                                 uint16_t addr_6502,
                                 int32_t uopcode,
                                 int32_t value1) {
  (void) memset(p_opcode, '\0', sizeof(struct jit_opcode_details));
  /* It's covered by the memset() and "internal" opcodes have
   * len_bytes_6502_orig == 0.
   */
  p_opcode->addr_6502 = addr_6502;
  p_opcode->cycles_run_start = -1;
  p_opcode->num_uops = 1;
  jit_opcode_make_uop1(&p_opcode->uops[0], uopcode, value1);
}

void
jit_opcode_replace1(struct jit_opcode_details* p_opcode,
                    int32_t uop1,
                    int32_t value1) {
  p_opcode->num_uops = 1;
  jit_opcode_make_uop1(&p_opcode->uops[0], uop1, value1);
}

void
jit_opcode_replace2(struct jit_opcode_details* p_opcode,
                    int32_t uop1,
                    int32_t value1,
                    int32_t uop2,
                    int32_t value2) {
  p_opcode->num_uops = 2;
  jit_opcode_make_uop1(&p_opcode->uops[0], uop1, value1);
  jit_opcode_make_uop1(&p_opcode->uops[1], uop2, value2);
}

void
jit_opcode_replace3(struct jit_opcode_details* p_opcode,
                    int32_t uop1,
                    int32_t value1,
                    int32_t uop2,
                    int32_t value2,
                    int32_t uop3,
                    int32_t value3) {
  p_opcode->num_uops = 3;
  jit_opcode_make_uop1(&p_opcode->uops[0], uop1, value1);
  jit_opcode_make_uop1(&p_opcode->uops[1], uop2, value2);
  jit_opcode_make_uop1(&p_opcode->uops[2], uop3, value3);
}

void
jit_opcode_make_uop1(struct jit_uop* p_uop, int32_t uopcode, int value1) {
  (void) memset(p_uop, '\0', sizeof(struct jit_uop));
  p_uop->uopcode = uopcode;
  /* TODO: get rid of? */
  p_uop->uoptype = -1;
  p_uop->value1 = value1;
}

struct jit_uop*
jit_opcode_find_uop(struct jit_opcode_details* p_opcode, int32_t uopcode) {
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
