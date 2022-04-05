#include "asm_util.h"

#include "asm_opcodes.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

void
asm_make_uop0(struct asm_uop* p_uop, int32_t uopcode) {
  p_uop->uopcode = uopcode;
  p_uop->value1 = 0;
  p_uop->value2 = 0;
  p_uop->is_eliminated = 0;
  p_uop->is_merged = 0;
  p_uop->backend_tag = 0;
}

void
asm_make_uop1(struct asm_uop* p_uop, int32_t uopcode, intptr_t value1) {
  p_uop->uopcode = uopcode;
  p_uop->value1 = value1;
  p_uop->value2 = 0;
  p_uop->is_eliminated = 0;
  p_uop->is_merged = 0;
  p_uop->backend_tag = 0;
}

struct asm_uop*
asm_find_uop(int32_t* p_out_index,
             struct asm_uop* p_uops,
             uint32_t num_uops,
             int32_t uopcode) {
  uint32_t i_uops;
  *p_out_index = -1;
  for (i_uops = 0; i_uops < num_uops; ++i_uops) {
    struct asm_uop* p_uop = &p_uops[i_uops];
    if (p_uop->uopcode != uopcode) {
      continue;
    }
    *p_out_index = i_uops;
    return p_uop;
  }
  return NULL;
}

struct asm_uop*
asm_insert_uop(struct asm_uop* p_uops, uint32_t num_uops, uint32_t index) {
  assert(index <= num_uops);
  (void) memmove(&p_uops[index + 1],
                 &p_uops[index],
                 ((num_uops - index) * sizeof(struct asm_uop)));
  return &p_uops[index];
}

/* NOTE: this routine is tightly coupled to the way the 6502 JIT client issues
 * sequences of uopcodes. This is not a particularly correct design, but it
 * does lead to simplicity in the rewriters, so we'll take it for now.
 */
void
asm_breakdown_from_6502(struct asm_uop* p_uops,
                        uint32_t num_uops,
                        struct asm_uop** p_out_main,
                        struct asm_uop** p_out_mode,
                        struct asm_uop** p_out_load,
                        struct asm_uop** p_out_store,
                        struct asm_uop** p_out_load_carry,
                        struct asm_uop** p_out_save_carry,
                        struct asm_uop** p_out_load_overflow,
                        struct asm_uop** p_out_nz_flags,
                        struct asm_uop** p_out_inv,
                        struct asm_uop** p_out_addr_check,
                        struct asm_uop** p_out_page_crossing) {
  uint32_t i;

  *p_out_main = NULL;
  *p_out_mode = NULL;
  *p_out_load = NULL;
  *p_out_store = NULL;
  *p_out_load_carry = NULL;
  *p_out_save_carry = NULL;
  *p_out_load_overflow = NULL;
  *p_out_nz_flags = NULL;
  *p_out_inv = NULL;
  *p_out_addr_check = NULL;
  *p_out_page_crossing = NULL;

  for (i = 0; i < num_uops; ++i) {
    struct asm_uop* p_uop = &p_uops[i];
    int32_t uopcode = p_uop->uopcode;
    switch (uopcode) {
    case k_opcode_value_set:
      assert((i + 1) < num_uops);
      *p_out_mode = p_uop;
      break;
    case k_opcode_addr_set:
      assert((i + 1) < num_uops);
      *p_out_mode = p_uop;
      break;
    case k_opcode_value_load_16bit_wrap:
    case k_opcode_addr_load_16bit_wrap:
    case k_opcode_addr_load_16bit_nowrap:
    case k_opcode_addr_load_8bit:
    case k_opcode_addr_add_x_8bit:
    case k_opcode_addr_add_y_8bit:
    case k_opcode_addr_add_x:
    case k_opcode_addr_add_y:
    case k_opcode_addr_add_base_y:
    case k_opcode_addr_add_base_constant:
      assert((i + 1) < num_uops);
      assert(i != 0);
      *p_out_mode = p_uop;
      break;
    case k_opcode_value_load:
      assert((i + 1) < num_uops);
      assert(i != 0);
      assert(*p_out_load == NULL);
      *p_out_load = p_uop;
      break;
    case k_opcode_value_store:
      assert(i != 0);
      assert(*p_out_store == NULL);
      *p_out_store = p_uop;
      break;
    case k_opcode_load_carry:
      assert((i + 1) < num_uops);
      assert(*p_out_load_carry == NULL);
      *p_out_load_carry = p_uop;
      break;
    case k_opcode_save_carry:
      assert(i != 0);
      assert(*p_out_save_carry == NULL);
      *p_out_save_carry = p_uop;
      break;
    case k_opcode_load_overflow:
      assert((i + 1) < num_uops);
      assert(*p_out_load_overflow == NULL);
      *p_out_load_overflow = p_uop;
      break;
    case k_opcode_flags_nz_a:
    case k_opcode_flags_nz_x:
    case k_opcode_flags_nz_y:
    case k_opcode_flags_nz_value:
      assert(i != 0);
      assert(*p_out_nz_flags == NULL);
      *p_out_nz_flags = p_uop;
      break;
    case k_opcode_addr_check:
      assert(i != 0);
      assert(*p_out_addr_check == NULL);
      *p_out_addr_check = p_uop;
      break;
    case k_opcode_write_inv:
      assert(i != 0);
      assert(*p_out_inv == NULL);
      *p_out_inv = p_uop;
      break;
    case k_opcode_check_page_crossing_x:
    case k_opcode_check_page_crossing_y:
      assert(*p_out_page_crossing == NULL);
      *p_out_page_crossing = p_uop;
      break;
    default:
      if ((uopcode >= k_opcode_main_begin) && (uopcode <= k_opcode_main_end)) {
        if (*p_out_main == NULL) {
          *p_out_main = p_uop;
        }
      }
      break;
    }
  }
}
