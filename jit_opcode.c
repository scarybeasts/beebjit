#include "jit_opcode.h"

#include "util.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

void
jit_opcode_find_replace1(struct jit_opcode_details* p_opcode,
                         int32_t find_uop,
                         int32_t uop1,
                         int32_t value1) {
  struct jit_uop* p_uop = jit_opcode_find_uop(p_opcode, find_uop);
  assert(p_uop != NULL);
  jit_opcode_make_uop1(p_uop, uop1, value1);
}

void
jit_opcode_find_replace2(struct jit_opcode_details* p_opcode,
                         int32_t find_uop,
                         int32_t uop1,
                         int32_t value1,
                         int32_t uop2,
                         int32_t value2) {
  uint32_t index;
  struct jit_uop* p_uop = jit_opcode_find_uop(p_opcode, find_uop);
  assert(p_uop != NULL);

  if (p_opcode->num_uops == k_max_uops_per_opcode) {
    util_bail("uops full");
  }

  jit_opcode_make_uop1(p_uop, uop1, value1);
  index = (p_uop - &p_opcode->uops[0]);
  jit_opcode_insert_uop(p_opcode, (index + 1), uop2, value2);
}

void
jit_opcode_make_uop1(struct jit_uop* p_uop, int32_t uopcode, int value1) {
  (void) memset(p_uop, '\0', sizeof(struct jit_uop));
  p_uop->uopcode = uopcode;
  /* TODO: get rid of? */
  p_uop->uoptype = -1;
  p_uop->uopmode = -1;
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

void
jit_opcode_erase_uop(struct jit_opcode_details* p_opcode, int32_t uopcode) {
  uint32_t i;
  struct jit_uop* p_uop = jit_opcode_find_uop(p_opcode, uopcode);

  assert(p_uop != NULL);

  i = (p_uop - &p_opcode->uops[0]);
  (void) memmove(p_uop,
                 (p_uop + 1),
                 ((p_opcode->num_uops - i - 1) * sizeof(struct jit_uop)));
  p_opcode->num_uops--;
}

void
jit_opcode_insert_uop(struct jit_opcode_details* p_opcode,
                      uint32_t index,
                      int32_t uopcode,
                      int32_t value) {
  if (p_opcode->num_uops == k_max_uops_per_opcode) {
    util_bail("uops full");
  }
  assert(index <= p_opcode->num_uops);

  (void) memmove(&p_opcode->uops[index + 1],
                 &p_opcode->uops[index],
                 ((p_opcode->num_uops - index) * sizeof(struct jit_uop)));
  p_opcode->num_uops++;
  jit_opcode_make_uop1(&p_opcode->uops[index], uopcode, value);
}

void
jit_opcode_eliminate(struct jit_opcode_details* p_opcode) {
  uint32_t i;
  uint32_t num_uops = p_opcode->num_uops;

  for (i = 0; i < num_uops; ++i) {
    struct jit_uop* p_uop = &p_opcode->uops[i];
    if (!p_uop->is_prefix_or_postfix) {
      p_uop->eliminated = 1;
    }
  }

  p_opcode->len_bytes_6502_merged = 0;
  p_opcode->max_cycles_merged = 0;
}
