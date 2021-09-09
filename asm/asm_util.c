#include "asm_util.h"

#include "asm_opcodes.h"

#include <assert.h>
#include <stddef.h>

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
                        struct asm_uop** p_out_flags,
                        struct asm_uop** p_out_inv,
                        struct asm_uop** p_out_addr_check) {
  uint32_t i;

  *p_out_main = NULL;
  *p_out_mode = NULL;
  *p_out_load = NULL;
  *p_out_store = NULL;
  *p_out_load_carry = NULL;
  *p_out_save_carry = NULL;
  *p_out_flags = NULL;
  *p_out_inv = NULL;
  *p_out_addr_check = NULL;

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
    case k_opcode_addr_add_x_8bit:
    case k_opcode_addr_add_y_8bit:
    case k_opcode_addr_add_x:
    case k_opcode_addr_add_y:
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
    case k_opcode_flags_nz_a:
    case k_opcode_flags_nz_x:
    case k_opcode_flags_nz_y:
    case k_opcode_flags_nz_value:
      assert(i != 0);
      assert(*p_out_flags == NULL);
      *p_out_flags = p_uop;
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
    default:
      if ((uopcode >= k_opcode_main_begin) && (uopcode <= k_opcode_main_end)) {
        *p_out_main = p_uop;
      }
      break;
    }
  }
}
