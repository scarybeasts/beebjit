#include "jit_opcode.h"

#include "util.h"

#include "asm/asm_util.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

void
jit_opcode_find_replace1(struct jit_opcode_details* p_opcode,
                         int32_t find_uop,
                         int32_t uop1,
                         int32_t value1) {
  int32_t index;
  struct asm_uop* p_uop = jit_opcode_find_uop(p_opcode, &index, find_uop);
  assert(p_uop != NULL);
  asm_make_uop1(p_uop, uop1, value1);
}

void
jit_opcode_find_replace2(struct jit_opcode_details* p_opcode,
                         int32_t find_uop,
                         int32_t uop1,
                         int32_t value1,
                         int32_t uop2,
                         int32_t value2) {
  int32_t index;
  struct asm_uop* p_uop = jit_opcode_find_uop(p_opcode, &index, find_uop);
  assert(p_uop != NULL);

  if (p_opcode->num_uops == k_max_uops_per_opcode) {
    util_bail("uops full");
  }

  asm_make_uop1(p_uop, uop1, value1);
  p_uop = jit_opcode_insert_uop(p_opcode, (index + 1));
  asm_make_uop1(p_uop, uop2, value2);
}

struct asm_uop*
jit_opcode_find_uop(struct jit_opcode_details* p_opcode,
                    int32_t* p_out_index,
                    int32_t uopcode) {
  return asm_find_uop(p_out_index,
                      &p_opcode->uops[0],
                      p_opcode->num_uops,
                      uopcode);
}

void
jit_opcode_erase_uop(struct jit_opcode_details* p_opcode, int32_t uopcode) {
  int32_t index;
  struct asm_uop* p_uop = jit_opcode_find_uop(p_opcode, &index, uopcode);

  assert(p_uop != NULL);

  (void) memmove(p_uop,
                 (p_uop + 1),
                 ((p_opcode->num_uops - index - 1) * sizeof(struct asm_uop)));
  p_opcode->num_uops--;
}

struct asm_uop*
jit_opcode_insert_uop(struct jit_opcode_details* p_opcode, uint32_t index) {
  struct asm_uop* p_ret;
  uint32_t num_uops = p_opcode->num_uops;
  if (num_uops == k_max_uops_per_opcode) {
    util_bail("uops full");
  }
  p_ret = asm_insert_uop(&p_opcode->uops[0], num_uops, index);
  p_opcode->num_uops++;
  return p_ret;
}

void
jit_opcode_eliminate(struct jit_opcode_details* p_opcode) {
  uint32_t i;
  uint32_t num_uops = p_opcode->num_uops;

  for (i = 0; i < num_uops; ++i) {
    struct asm_uop* p_uop;
    if (i == 0) {
      if (p_opcode->has_prefix_uop) {
        continue;
      }
    } else if (i == (num_uops - 1)) {
      if (p_opcode->has_postfix_uop) {
        continue;
      }
    }
    p_uop = &p_opcode->uops[i];
    p_uop->is_eliminated = 1;
  }
}
