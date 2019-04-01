#include "jit_compiler.h"

#include "asm_x64_common.h"
#include "asm_x64_jit.h"
#include "asm_x64_jit_defs.h"
#include "defs_6502.h"
#include "memory_access.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

struct jit_compiler {
  struct memory_access* p_memory_access;
  uint8_t* p_mem_read;
  void* (*get_block_host_address)(void*, uint16_t);
  uint16_t (*get_jit_ptr_block)(void*, uint32_t);
  void* p_host_address_object;
  uint32_t* p_jit_ptrs;
  int debug;

  struct util_buffer* p_single_opcode_buf;
  struct util_buffer* p_tmp_buf;
  uint32_t no_code_jit_ptr;

  int32_t reg_a;
  int32_t reg_x;
  int32_t reg_y;
  int32_t flag_carry;
  int32_t flag_decimal;

  uint32_t len_x64_jmp;
  uint32_t len_x64_countdown;
};

struct jit_uop {
  int32_t opcode;
  int32_t value1;
  int32_t value2;
  int32_t optype;
};

struct jit_opcode_details {
  uint8_t opcode_6502;
  uint8_t len;
  uint8_t cycles;
  int branches;
  struct jit_uop uops[8];
};

static const int32_t k_value_unknown = -1;

enum {
  k_opcode_countdown = 0x100,
  k_opcode_debug,
  k_opcode_interp,
  k_opcode_ADD_IMM,
  k_opcode_ADD_Y_SCRATCH,
  k_opcode_CHECK_BCD,
  k_opcode_FLAGA,
  k_opcode_FLAGX,
  k_opcode_FLAGY,
  k_opcode_INC_SCRATCH,
  k_opcode_JMP_SCRATCH,
  k_opcode_LOAD_CARRY,
  k_opcode_LOAD_CARRY_INV,
  k_opcode_LOAD_OVERFLOW,
  k_opcode_MODE_ABX,
  k_opcode_MODE_ABY,
  k_opcode_MODE_IND,
  k_opcode_MODE_IND_SCRATCH,
  k_opcode_MODE_ZPX,
  k_opcode_MODE_ZPY,
  k_opcode_PULL_16,
  k_opcode_PUSH_16,
  k_opcode_SAVE_CARRY,
  k_opcode_SAVE_CARRY_INV,
  k_opcode_SAVE_OVERFLOW,
  k_opcode_STOA_IMM,
  k_opcode_SUB_IMM,
  k_opcode_WRITE_INV_ABS,
  k_opcode_WRITE_INV_SCRATCH,
};

static void
jit_set_jit_ptr_no_code(struct jit_compiler* p_compiler, uint16_t addr) {
  p_compiler->p_jit_ptrs[addr] = p_compiler->no_code_jit_ptr;
}

static void
jit_invalidate_jump_target(struct jit_compiler* p_compiler, uint16_t addr) {
  void* p_host_ptr =
      p_compiler->get_block_host_address(p_compiler->p_host_address_object,
                                         addr);
  util_buffer_setup(p_compiler->p_tmp_buf, p_host_ptr, 2);
  asm_x64_emit_jit_call_compile_trampoline(p_compiler->p_tmp_buf);
}

static void
jit_invalidate_block_with_addr(struct jit_compiler* p_compiler, uint16_t addr) {
  uint32_t jit_ptr = p_compiler->p_jit_ptrs[addr];
  uint16_t block_addr_6502 =
      p_compiler->get_jit_ptr_block(p_compiler->p_host_address_object, jit_ptr);
  jit_invalidate_jump_target(p_compiler, block_addr_6502);
}

struct jit_compiler*
jit_compiler_create(struct memory_access* p_memory_access,
                    void* (*get_block_host_address)(void*, uint16_t),
                    uint16_t (*get_jit_ptr_block)(void*, uint32_t),
                    void* p_host_address_object,
                    uint32_t* p_jit_ptrs,
                    int debug) {
  size_t i;
  struct util_buffer* p_tmp_buf;

  /* Check invariants required for compact code generation. */
  assert(K_JIT_CONTEXT_OFFSET_JIT_PTRS < 0x80);

  struct jit_compiler* p_compiler = malloc(sizeof(struct jit_compiler));
  if (p_compiler == NULL) {
    errx(1, "cannot alloc jit_compiler");
  }
  (void) memset(p_compiler, '\0', sizeof(struct jit_compiler));

  p_compiler->p_memory_access = p_memory_access;
  p_compiler->p_mem_read = p_memory_access->p_mem_read;
  p_compiler->get_block_host_address = get_block_host_address;
  p_compiler->get_jit_ptr_block = get_jit_ptr_block;
  p_compiler->p_host_address_object = p_host_address_object;
  p_compiler->p_jit_ptrs = p_jit_ptrs;
  p_compiler->debug = debug;

  p_compiler->p_single_opcode_buf = util_buffer_create();
  p_tmp_buf = util_buffer_create();
  p_compiler->p_tmp_buf = p_tmp_buf;

  p_compiler->no_code_jit_ptr = 
      (uint32_t) (size_t) get_block_host_address(p_host_address_object,
                                                 (k_6502_addr_space_size - 1));

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    jit_set_jit_ptr_no_code(p_compiler, i);
  }

  /* Calculate lengths of sequences we need to know. */
  p_compiler->len_x64_jmp = (asm_x64_jit_JMP_END - asm_x64_jit_JMP);
  p_compiler->len_x64_countdown = (asm_x64_jit_check_countdown_END -
                                   asm_x64_jit_check_countdown);

  return p_compiler;
}

void
jit_compiler_destroy(struct jit_compiler* p_compiler) {
  util_buffer_destroy(p_compiler->p_single_opcode_buf);
  util_buffer_destroy(p_compiler->p_tmp_buf);
  free(p_compiler);
}

static uint8_t
jit_compiler_get_opcode_details(struct jit_compiler* p_compiler,
                                struct jit_opcode_details* p_details,
                                uint16_t addr_6502) {
  uint8_t opcode_6502;
  uint8_t optype;
  uint8_t opmode;
  uint8_t opmem;
  struct jit_uop* p_main_uop;
  uint16_t addr_range_start;
  uint16_t addr_range_end;

  struct memory_access* p_memory_access = p_compiler->p_memory_access;
  uint8_t* p_mem_read = p_compiler->p_mem_read;
  void* p_memory_callback = p_memory_access->p_callback_obj;
  uint16_t addr_plus_1 = (addr_6502 + 1);
  uint16_t addr_plus_2 = (addr_6502 + 2);
  int jump_fixup = 0;
  int32_t main_value1 = -1;
  struct jit_uop* p_uop = &p_details->uops[0];
  struct jit_uop* p_first_post_debug_uop = p_uop;
  int use_interp = 0;

  opcode_6502 = p_mem_read[addr_6502];
  optype = g_optypes[opcode_6502];
  opmode = g_opmodes[opcode_6502];
  opmem = g_opmem[optype];

  p_details->opcode_6502 = opcode_6502;
  p_details->len = g_opmodelens[opmode];
  p_details->cycles = g_opcycles[opcode_6502];
  p_details->branches = g_opbranch[optype];

  if (p_compiler->debug) {
    p_uop->opcode = k_opcode_debug;
    p_uop->optype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
    p_first_post_debug_uop = p_uop;
  }

  /* Mode resolution and possibly per-mode uops. */
  switch (opmode) {
  case 0:
  case k_nil:
  case k_acc:
    break;
  case k_imm:
  case k_zpg:
    main_value1 = p_mem_read[addr_plus_1];
    break;
  case k_zpx:
    p_uop->opcode = k_opcode_MODE_ZPX;
    p_uop->value1 = p_mem_read[addr_plus_1];
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_zpy:
    p_uop->opcode = k_opcode_MODE_ZPY;
    p_uop->value1 = p_mem_read[addr_plus_1];
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_rel:
    main_value1 = ((int) addr_6502 + 2 + (int8_t) p_mem_read[addr_plus_1]);
    jump_fixup = 1;
    break;
  case k_abs:
  case k_abx:
  case k_aby:
    main_value1 = ((p_mem_read[addr_plus_2] << 8) | p_mem_read[addr_plus_1]);
    if (optype == k_jmp || optype == k_jsr) {
      jump_fixup = 1;
    }
    addr_range_start = main_value1;
    addr_range_end = main_value1;
    if (opmode == k_abx || opmode == k_aby) {
      addr_range_end += 0xFF;
    }
    if (opmem == k_read || opmem == k_rw) {
      if (p_memory_access->memory_read_needs_callback(p_memory_callback,
                                                      addr_range_start)) {
        use_interp = 1;
      }
      if (p_memory_access->memory_read_needs_callback(p_memory_callback,
                                                      addr_range_end)) {
        use_interp = 1;
      }
    }
    if (opmem == k_write || opmem == k_rw) {
      if (p_memory_access->memory_write_needs_callback(p_memory_callback,
                                                       addr_range_start)) {
        use_interp = 1;
      }
      if (p_memory_access->memory_write_needs_callback(p_memory_callback,
                                                       addr_range_end)) {
        use_interp = 1;
      }
    }
    break;
  case k_ind:
    p_uop->opcode = k_opcode_MODE_IND;
    p_uop->optype = -1;
    p_uop->value1 = ((p_mem_read[addr_plus_2] << 8) | p_mem_read[addr_plus_1]);
    p_uop++;
    break;
  case k_idx:
    p_uop->opcode = k_opcode_MODE_ZPX;
    p_uop->value1 = p_mem_read[addr_plus_1];
    p_uop->optype = -1;
    p_uop++;
    p_uop->opcode = k_opcode_MODE_IND_SCRATCH;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_idy:
    p_uop->opcode = k_opcode_MODE_IND;
    p_uop->value1 = (uint16_t) p_mem_read[addr_plus_1];
    p_uop->optype = -1;
    p_uop++;
    p_uop->opcode = k_opcode_ADD_Y_SCRATCH;
    p_uop->optype = -1;
    p_uop++;
    break;
  default:
    assert(0);
    break;
  }

  if (use_interp) {
    p_uop = p_first_post_debug_uop;

    p_uop->opcode = k_opcode_interp;
    p_uop->value1 = addr_6502;
    p_uop->optype = -1;
    p_uop++;
    return (p_uop - &p_details->uops[0]);
  }

  /* Code invalidation for writes, aka. self-modifying code. */
  /* TODO: only do zero page invalidations if the program needs it. */
  /* TODO: stack page invalidations. */
  if (opmem == k_write || opmem == k_rw) {
    switch (opmode) {
    case k_abs:
    case k_zpg:
      p_uop->opcode = k_opcode_WRITE_INV_ABS;
      p_uop->value1 = main_value1;
      p_uop->optype = -1;
      p_uop++;
      break;
    case k_abx:
      p_uop->opcode = k_opcode_MODE_ABX;
      p_uop->value1 = main_value1;
      p_uop->optype = -1;
      p_uop++;
      p_uop->opcode = k_opcode_WRITE_INV_SCRATCH;
      p_uop->optype = -1;
      p_uop++;
      break;
    case k_aby:
      p_uop->opcode = k_opcode_MODE_ABY;
      p_uop->value1 = main_value1;
      p_uop->optype = -1;
      p_uop++;
      p_uop->opcode = k_opcode_WRITE_INV_SCRATCH;
      p_uop->optype = -1;
      p_uop++;
      break;
    case k_idx:
    case k_idy:
    case k_zpx:
    case k_zpy:
      p_uop->opcode = k_opcode_WRITE_INV_SCRATCH;
      p_uop->optype = -1;
      p_uop++;
      break;
    default:
      break;
    }
  }

  /* Pre-main uops. */
  switch (optype) {
  case k_adc:
    p_uop->opcode = k_opcode_CHECK_BCD;
    p_uop->optype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
    p_uop->opcode = k_opcode_LOAD_CARRY;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_bcc:
  case k_bcs:
  case k_rol:
  case k_ror:
    p_uop->opcode = k_opcode_LOAD_CARRY;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_bvc:
  case k_bvs:
    p_uop->opcode = k_opcode_LOAD_OVERFLOW;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_jsr:
    p_uop->opcode = k_opcode_PUSH_16;
    p_uop->optype = -1;
    p_uop->value1 = (uint16_t) (addr_6502 + 2);
    p_uop++;
    break;
  case k_rti:
    /* PLP */
    p_uop->opcode = 0x28;
    p_uop->optype = k_plp;
    p_uop++;
    p_uop->opcode = k_opcode_PULL_16;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_rts:
    p_uop->opcode = k_opcode_PULL_16;
    p_uop->optype = -1;
    p_uop++;
    /* TODO: may increment 0xFFFF -> 0x10000, which may crash. */
    p_uop->opcode = k_opcode_INC_SCRATCH;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_sbc:
    p_uop->opcode = k_opcode_CHECK_BCD;
    p_uop->optype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
    p_uop->opcode = k_opcode_LOAD_CARRY_INV;
    p_uop->optype = -1;
    p_uop++;
    break;
  default:
    break;
  }

  /* Main uop. */
  p_main_uop = p_uop;
  p_main_uop->opcode = opcode_6502;
  p_main_uop->optype = optype;
  p_main_uop->value1 = main_value1;

  p_uop++;

  /* Post-main uops. */
  switch (optype) {
  case k_adc:
    p_uop->opcode = k_opcode_SAVE_CARRY;
    p_uop->optype = -1;
    p_uop++;
    p_uop->opcode = k_opcode_SAVE_OVERFLOW;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_alr:
  case k_asl:
  case k_lsr:
  case k_slo:
    p_uop->opcode = k_opcode_SAVE_CARRY;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_brk:
    /* Replace main uop with sequence. */
    p_main_uop->opcode = k_opcode_PUSH_16;
    p_main_uop->optype = -1;
    p_main_uop->value1 = (uint16_t) (addr_6502 + 2);
    /* PHP */
    p_uop->opcode = 0x08;
    p_uop->optype = k_php;
    p_uop++;
    /* SEI */
    p_uop->opcode = 0x78;
    p_uop->optype = k_sei;
    p_uop++;
    /* MODE_IND */
    p_uop->opcode = k_opcode_MODE_IND;
    p_uop->optype = -1;
    p_uop->value1 = (uint16_t) k_6502_vector_irq;
    p_uop++;
    /* JMP_SCRATCH */
    p_uop->opcode = k_opcode_JMP_SCRATCH;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_cmp:
  case k_cpx:
  case k_cpy:
    p_uop->opcode = k_opcode_SAVE_CARRY_INV;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_jmp:
    if (opmode == k_ind) {
      p_main_uop->opcode = k_opcode_JMP_SCRATCH;
      p_main_uop->optype = -1;
    }
    break;
  case k_jsr:
    p_main_uop->opcode = 0x4C; /* JMP abs */
    break;
  case k_lda:
  case k_txa:
  case k_tya:
  case k_pla:
    p_uop->opcode = k_opcode_FLAGA;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_ldx:
  case k_tax:
  case k_tsx:
    p_uop->opcode = k_opcode_FLAGX;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_ldy:
  case k_tay:
    p_uop->opcode = k_opcode_FLAGY;
    p_uop->optype = -1;
    p_uop++;
    break;
  case k_rol:
  case k_ror:
    p_uop->opcode = k_opcode_SAVE_CARRY;
    p_uop->optype = -1;
    p_uop++;
    /* TODO: come up with viable optimization for non-acc modes. */
    if (opmode == k_acc) {
      p_uop->opcode = k_opcode_FLAGA;
      p_uop->optype = -1;
      p_uop++;
    }
    break;
  case k_rti:
  case k_rts:
    p_main_uop->opcode = k_opcode_JMP_SCRATCH;
    p_main_uop->optype = -1;
    break;
  case k_sbc:
    p_uop->opcode = k_opcode_SAVE_CARRY_INV;
    p_uop->optype = -1;
    p_uop++;
    p_uop->opcode = k_opcode_SAVE_OVERFLOW;
    p_uop->optype = -1;
    p_uop++;
    break;
  default:
    break;
  }

  if (jump_fixup) {
    p_main_uop->value1 =
        (int32_t) (size_t) p_compiler->get_block_host_address(
            p_compiler->p_host_address_object,
            p_main_uop->value1);
  }

  return (p_uop - &p_details->uops[0]);
}

static void
jit_compiler_emit_uop(struct jit_compiler* p_compiler,
                      struct util_buffer* p_dest_buf,
                      struct jit_uop* p_uop) {
  int opcode = p_uop->opcode;
  int value1 = p_uop->value1;
  int value2 = p_uop->value2;
  struct memory_access* p_memory_access = p_compiler->p_memory_access;
  void* p_memory_object = p_memory_access->p_callback_obj;

  switch (opcode) {
  case k_opcode_countdown:
    asm_x64_emit_jit_check_countdown(p_dest_buf,
                                     (uint16_t) value1,
                                     (uint32_t) value2);
    break;
  case k_opcode_debug:
    asm_x64_emit_jit_call_debug(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_interp:
    asm_x64_emit_jit_jump_interp(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_ADD_Y_SCRATCH:
    asm_x64_emit_jit_ADD_Y_SCRATCH(p_dest_buf);
    break;
  case k_opcode_ADD_IMM:
    asm_x64_emit_jit_ADD_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_CHECK_BCD:
    asm_x64_emit_jit_CHECK_BCD(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_FLAGA:
    asm_x64_emit_jit_FLAGA(p_dest_buf);
    break;
  case k_opcode_FLAGX:
    asm_x64_emit_jit_FLAGX(p_dest_buf);
    break;
  case k_opcode_FLAGY:
    asm_x64_emit_jit_FLAGY(p_dest_buf);
    break;
  case k_opcode_MODE_ABX:
    asm_x64_emit_jit_MODE_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_MODE_ABY:
    asm_x64_emit_jit_MODE_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_MODE_IND:
    asm_x64_emit_jit_MODE_IND(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_MODE_IND_SCRATCH:
    asm_x64_emit_jit_MODE_IND_SCRATCH(p_dest_buf);
    break;
  case k_opcode_MODE_ZPX:
    asm_x64_emit_jit_MODE_ZPX(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_MODE_ZPY:
    asm_x64_emit_jit_MODE_ZPY(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_INC_SCRATCH:
    asm_x64_emit_jit_INC_SCRATCH(p_dest_buf);
    break;
  case k_opcode_JMP_SCRATCH:
    asm_x64_emit_jit_JMP_SCRATCH(p_dest_buf);
    break;
  case k_opcode_LOAD_CARRY:
    asm_x64_emit_jit_LOAD_CARRY(p_dest_buf);
    break;
  case k_opcode_LOAD_CARRY_INV:
    asm_x64_emit_jit_LOAD_CARRY_INV(p_dest_buf);
    break;
  case k_opcode_LOAD_OVERFLOW:
    asm_x64_emit_jit_LOAD_OVERFLOW(p_dest_buf);
    break;
  case k_opcode_PULL_16:
    asm_x64_emit_pull_word_to_scratch(p_dest_buf);
    break;
  case k_opcode_PUSH_16:
    asm_x64_emit_jit_PUSH_16(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_SAVE_CARRY:
    asm_x64_emit_jit_SAVE_CARRY(p_dest_buf);
    break;
  case k_opcode_SAVE_CARRY_INV:
    asm_x64_emit_jit_SAVE_CARRY_INV(p_dest_buf);
    break;
  case k_opcode_SAVE_OVERFLOW:
    asm_x64_emit_jit_SAVE_OVERFLOW(p_dest_buf);
    break;
  case k_opcode_STOA_IMM:
    asm_x64_emit_jit_STOA_IMM(p_dest_buf, (uint16_t) value1, (uint8_t) value2);
    break;
  case k_opcode_SUB_IMM:
    asm_x64_emit_jit_SUB_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case k_opcode_WRITE_INV_ABS:
    asm_x64_emit_jit_WRITE_INV_ABS(p_dest_buf, (uint32_t) value1);
    break;
  case k_opcode_WRITE_INV_SCRATCH:
    asm_x64_emit_jit_WRITE_INV_SCRATCH(p_dest_buf);
    break;
  case 0x01: /* ORA idx */
  case 0x11: /* ORA idy */
  case 0x15: /* ORA zpx */
    asm_x64_emit_jit_ORA_scratch(p_dest_buf);
    break;
  case 0x02:
    asm_x64_emit_instruction_EXIT(p_dest_buf);
    break;
  case 0x04: /* NOP zpg */ /* Undocumented. */
  case 0xDC: /* NOP abx */ /* Undocumented. */
  case 0xEA: /* NOP */
  case 0xF4: /* NOP zpx */ /* Undocumented. */
    /* We don't really have to emit anything for a NOP, but for now and for
     * good readability, we'll emit a host NOP.
     */
    asm_x64_emit_instruction_REAL_NOP(p_dest_buf);
    break;
  case 0x05: /* ORA zpg */
  case 0x0D: /* ORA abs */
    asm_x64_emit_jit_ORA_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x06: /* ASL zpg */
    asm_x64_emit_jit_ASL_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x07: /* SLO zpg */ /* Undocumented. */
    asm_x64_emit_jit_SLO_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x08:
    asm_x64_emit_instruction_PHP(p_dest_buf);
    break;
  case 0x09:
    asm_x64_emit_jit_ORA_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x0A:
    asm_x64_emit_jit_ASL_ACC(p_dest_buf);
    break;
  case 0x0E: /* ASL abs */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1)) {
      asm_x64_emit_jit_ASL_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_ASL_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x10:
    asm_x64_emit_jit_BPL(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x16: /* ASL zpx */
    asm_x64_emit_jit_ASL_scratch(p_dest_buf);
    break;
  case 0x18:
    asm_x64_emit_instruction_CLC(p_dest_buf);
    break;
  case 0x19:
    asm_x64_emit_jit_ORA_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0x1D:
    asm_x64_emit_jit_ORA_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x1E: /* ASL abx */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1) &&
        p_memory_access->memory_is_always_ram(p_memory_object,
                                              (value1 + 0xFF))) {
      asm_x64_emit_jit_ASL_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_ASL_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x21: /* AND idx */
  case 0x31: /* AND idy */
  case 0x35: /* AND zpx */
    asm_x64_emit_jit_AND_scratch(p_dest_buf);
    break;
  case 0x24: /* BIT zpg */
  case 0x2C: /* BIT abs */
    asm_x64_emit_jit_BIT(p_dest_buf, (uint16_t) value1);
    break;
  case 0x25: /* AND zpg */
  case 0x2D: /* AND abs */
    asm_x64_emit_jit_AND_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x26: /* ROL zpg */
  case 0x2E: /* ROL abs */
    asm_x64_emit_jit_ROL_ABS_RMW(p_dest_buf, (uint16_t) value1);
    break;
  case 0x28:
    asm_x64_emit_instruction_PLP(p_dest_buf);
    break;
  case 0x29:
    asm_x64_emit_jit_AND_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x2A:
    asm_x64_emit_jit_ROL_ACC(p_dest_buf);
    break;
  case 0x30:
    asm_x64_emit_jit_BMI(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x36: /* ROL zpx */
    asm_x64_emit_jit_ROL_scratch(p_dest_buf);
    break;
  case 0x38:
    asm_x64_emit_instruction_SEC(p_dest_buf);
    break;
  case 0x39:
    asm_x64_emit_jit_AND_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0x3D:
    asm_x64_emit_jit_AND_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x3E:
    asm_x64_emit_jit_ROL_ABX_RMW(p_dest_buf, (uint16_t) value1);
    break;
  case 0x41: /* EOR idx */
  case 0x51: /* EOR idy */
  case 0x55: /* EOR zpx */
    asm_x64_emit_jit_EOR_scratch(p_dest_buf);
    break;
  case 0x45: /* EOR zpg */
  case 0x4D: /* EOR abs */
    asm_x64_emit_jit_EOR_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x46: /* LSR zpg */
    asm_x64_emit_jit_LSR_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x48:
    asm_x64_emit_instruction_PHA(p_dest_buf);
    break;
  case 0x49:
    asm_x64_emit_jit_EOR_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x4A:
    asm_x64_emit_jit_LSR_ACC(p_dest_buf);
    break;
  case 0x4B: /* ALR imm */ /* Undocumented. */
    asm_x64_emit_jit_ALR_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x4C:
    asm_x64_emit_jit_JMP(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x4E: /* LSR abs */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1)) {
      asm_x64_emit_jit_LSR_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_LSR_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x50:
    asm_x64_emit_jit_BVC(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x56: /* LSR zpx */
    asm_x64_emit_jit_LSR_scratch(p_dest_buf);
    break;
  case 0x58:
    asm_x64_emit_instruction_CLI(p_dest_buf);
    break;
  case 0x59:
    asm_x64_emit_jit_EOR_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0x5D:
    asm_x64_emit_jit_EOR_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x5E: /* LSR abx */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1) &&
        p_memory_access->memory_is_always_ram(p_memory_object,
                                              (value1 + 0xFF))) {
      asm_x64_emit_jit_LSR_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_LSR_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0x61: /* ADC idx */
  case 0x71: /* ADC idy */
  case 0x75: /* ADC zpx */
    asm_x64_emit_jit_ADC_scratch(p_dest_buf);
    break;
  case 0x65: /* ADC zpg */
  case 0x6D: /* ADC abs */
    asm_x64_emit_jit_ADC_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x66: /* ROR zpg */
  case 0x6E: /* ROR abs */
    asm_x64_emit_jit_ROR_ABS_RMW(p_dest_buf, (uint16_t) value1);
    break;
  case 0x68:
    asm_x64_emit_instruction_PLA(p_dest_buf);
    break;
  case 0x69:
    asm_x64_emit_jit_ADC_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0x6A:
    asm_x64_emit_jit_ROR_ACC(p_dest_buf);
    break;
  case 0x70:
    asm_x64_emit_jit_BVS(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x76: /* ROR zpx */
    asm_x64_emit_jit_ROR_scratch(p_dest_buf);
    break;
  case 0x78:
    asm_x64_emit_instruction_SEI(p_dest_buf);
    break;
  case 0x79:
    asm_x64_emit_jit_ADC_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0x7D:
    asm_x64_emit_jit_ADC_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x7E:
    asm_x64_emit_jit_ROR_ABX_RMW(p_dest_buf, (uint16_t) value1);
    break;
  case 0x81: /* STA idx */
  case 0x91: /* STA idy */
  case 0x95: /* STA zpx */
    asm_x64_emit_jit_STA_scratch(p_dest_buf);
    break;
  case 0x98:
    asm_x64_emit_instruction_TYA(p_dest_buf);
    break;
  case 0x84: /* STY zpg */
  case 0x8C: /* STY abs */
    asm_x64_emit_jit_STY_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x85: /* STA zpg */
  case 0x8D: /* STA abs */
    asm_x64_emit_jit_STA_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x86: /* STX zpg */
  case 0x8E: /* STX abs */
    asm_x64_emit_jit_STX_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x87: /* SAX zpg */ /* Undocumented. */
    asm_x64_emit_jit_SAX_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0x88:
    asm_x64_emit_instruction_DEY(p_dest_buf);
    break;
  case 0x8A:
    asm_x64_emit_instruction_TXA(p_dest_buf);
    break;
  case 0x90:
    asm_x64_emit_jit_BCC(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x94: /* STY zpx */
    asm_x64_emit_jit_STY_scratch(p_dest_buf);
    break;
  case 0x96: /* STX zpy */
    asm_x64_emit_jit_STX_scratch(p_dest_buf);
    break;
  case 0x99:
    asm_x64_emit_jit_STA_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0x9A:
    asm_x64_emit_instruction_TXS(p_dest_buf);
    break;
  case 0x9C:
    asm_x64_emit_jit_SHY_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0x9D:
    asm_x64_emit_jit_STA_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xA0:
    asm_x64_emit_jit_LDY_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xA1: /* LDA idx */
  case 0xB1: /* LDA idy */
  case 0xB5: /* LDA zpx */
    asm_x64_emit_jit_LDA_scratch(p_dest_buf);
    break;
  case 0xA2:
    asm_x64_emit_jit_LDX_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xA4: /* LDY zpg */
  case 0xAC: /* LDY abs */
    asm_x64_emit_jit_LDY_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xA5: /* LDA zpg */
  case 0xAD: /* LDA abs */
    asm_x64_emit_jit_LDA_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xA6: /* LDX zpg */
  case 0xAE: /* LDX abs */
    asm_x64_emit_jit_LDX_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xA8:
    asm_x64_emit_instruction_TAY(p_dest_buf);
    break;
  case 0xA9:
    asm_x64_emit_jit_LDA_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xAA:
    asm_x64_emit_instruction_TAX(p_dest_buf);
    break;
  case 0xB0:
    asm_x64_emit_jit_BCS(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xB4: /* LDY zpx */
    asm_x64_emit_jit_LDY_scratch(p_dest_buf);
    break;
  case 0xB6: /* LDX zpy */
    asm_x64_emit_jit_LDX_scratch(p_dest_buf);
    break;
  case 0xB8:
    asm_x64_emit_instruction_CLV(p_dest_buf);
    break;
  case 0xB9:
    asm_x64_emit_jit_LDA_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0xBA:
    asm_x64_emit_instruction_TSX(p_dest_buf);
    break;
  case 0xBC:
    asm_x64_emit_jit_LDY_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xBD:
    asm_x64_emit_jit_LDA_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xBE:
    asm_x64_emit_jit_LDX_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0xC0:
    asm_x64_emit_jit_CPY_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xC1: /* CMP idx */
  case 0xD1: /* CMP idy */
  case 0xD5: /* CMP zpx */
    asm_x64_emit_jit_CMP_scratch(p_dest_buf);
    break;
  case 0xC4: /* CPY zpg */
  case 0xCC: /* CPY abs */
    asm_x64_emit_jit_CPY_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xC5: /* CMP zpg */
  case 0xCD: /* CMP abs */
    asm_x64_emit_jit_CMP_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xC6: /* DEC zpg */
    asm_x64_emit_jit_DEC_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xC8:
    asm_x64_emit_instruction_INY(p_dest_buf);
    break;
  case 0xC9:
    asm_x64_emit_jit_CMP_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xCA:
    asm_x64_emit_instruction_DEX(p_dest_buf);
    break;
  case 0xCE: /* DEC abs */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1)) {
      asm_x64_emit_jit_DEC_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_DEC_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0xD0:
    asm_x64_emit_jit_BNE(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xD6: /* DEC zpx */
    asm_x64_emit_jit_DEC_scratch(p_dest_buf);
    break;
  case 0xD8:
    asm_x64_emit_instruction_CLD(p_dest_buf);
    break;
  case 0xD9:
    asm_x64_emit_jit_CMP_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0xDD:
    asm_x64_emit_jit_CMP_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xDE: /* DEC abx */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1) &&
        p_memory_access->memory_is_always_ram(p_memory_object,
                                              (value1 + 0xFF))) {
      asm_x64_emit_jit_DEC_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_DEC_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0xE0:
    asm_x64_emit_jit_CPX_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xE1: /* SBC idx */
  case 0xF1: /* SBC idy */
  case 0xF5: /* SBC zpx */
    asm_x64_emit_jit_SBC_scratch(p_dest_buf);
    break;
  case 0xE4: /* CPX zpg */
  case 0xEC: /* CPX abs */
    asm_x64_emit_jit_CPX_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xE5: /* SBC zpg */
  case 0xED: /* SBC abs */
    asm_x64_emit_jit_SBC_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xE6: /* INC zpg */
    asm_x64_emit_jit_INC_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xE8:
    asm_x64_emit_instruction_INX(p_dest_buf);
    break;
  case 0xE9:
    asm_x64_emit_jit_SBC_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xEE: /* INC abs */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1)) {
      asm_x64_emit_jit_INC_ABS(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_INC_ABS_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  case 0xF0:
    asm_x64_emit_jit_BEQ(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xF2:
    asm_x64_emit_instruction_CRASH(p_dest_buf);
    break;
  case 0xF6: /* INC zpx */
    asm_x64_emit_jit_INC_scratch(p_dest_buf);
    break;
  case 0xF8:
    asm_x64_emit_instruction_SED(p_dest_buf);
    break;
  case 0xF9:
    asm_x64_emit_jit_SBC_ABY(p_dest_buf, (uint16_t) value1);
    break;
  case 0xFD:
    asm_x64_emit_jit_SBC_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xFE: /* INC abx */
    if (p_memory_access->memory_is_always_ram(p_memory_object, value1) &&
        p_memory_access->memory_is_always_ram(p_memory_object,
                                              (value1 + 0xFF))) {
      asm_x64_emit_jit_INC_ABX(p_dest_buf, (uint16_t) value1);
    } else {
      asm_x64_emit_jit_INC_ABX_RMW(p_dest_buf, (uint16_t) value1);
    }
    break;
  default:
    asm_x64_emit_instruction_ILLEGAL(p_dest_buf);
    break;
  }
}

static void
jit_compiler_process_uop(struct jit_compiler* p_compiler,
                         struct util_buffer* p_dest_buf,
                         struct jit_uop* p_uop) {
  int32_t opcode = p_uop->opcode;
  int32_t optype = p_uop->optype;
  int32_t value1 = p_uop->value1;
  int32_t opreg = -1;
  int32_t changes_carry = 0;

  if (optype != -1) {
    assert(opcode < 256);
    /* TODO: seems hacky, should g_optype_sets_register just be per-opcode? */
    opreg = g_optype_sets_register[optype];
    if (g_opmodes[opcode] == k_acc) {
      opreg = k_a;
    }
    changes_carry = g_optype_changes_carry[optype];
  }

  /* Re-write the opcode if we have an optimization opportunity. */
  switch (opcode) {
  case 0x69: /* ADC imm */
    if (p_compiler->flag_carry == 0) {
      p_uop->opcode = k_opcode_ADD_IMM;
    }
    break;
  case 0x84: /* STY zpg */
  case 0x8C: /* STY abs */
    if (p_compiler->reg_y != k_value_unknown) {
      p_uop->opcode = k_opcode_STOA_IMM;
      p_uop->value2 = p_compiler->reg_y;
    }
    break;
  case 0x85: /* STA zpg */
  case 0x8D: /* STA abs */
    if (p_compiler->reg_a != k_value_unknown) {
      p_uop->opcode = k_opcode_STOA_IMM;
      p_uop->value2 = p_compiler->reg_a;
    }
    break;
  case 0x86: /* STX zpg */
  case 0x8E: /* STX abs */
    if (p_compiler->reg_x != k_value_unknown) {
      p_uop->opcode = k_opcode_STOA_IMM;
      p_uop->value2 = p_compiler->reg_x;
    }
    break;
  case 0xE9: /* SBC imm */
    if (p_compiler->flag_carry == 1) {
      p_uop->opcode = k_opcode_SUB_IMM;
    }
    break;
  default:
    break;
  }

  jit_compiler_emit_uop(p_compiler, p_dest_buf, p_uop);

  /* Update known state of registers, flags, etc. */
  switch (opreg) {
  case k_a:
    p_compiler->reg_a = k_value_unknown;
    break;
  case k_x:
    p_compiler->reg_x = k_value_unknown;
    break;
  case k_y:
    p_compiler->reg_y = k_value_unknown;
    break;
  default:
    break;
  }

  if (changes_carry) {
    p_compiler->flag_carry = k_value_unknown;
  }

  switch (opcode) {
  case 0x18: /* CLC */
    p_compiler->flag_carry = 0;
    break;
  case 0x38: /* SEC */
    p_compiler->flag_carry = 1;
    break;
  case 0xA0: /* LDY imm */
    p_compiler->reg_y = value1;
    break;
  case 0xA2: /* LDX imm */
    p_compiler->reg_x = value1;
    break;
  case 0xA9: /* LDA imm */
    p_compiler->reg_a = value1;
    break;
  case 0xD8: /* CLD */
    p_compiler->flag_decimal = 0;
    break;
  case 0xF8: /* SED */
    p_compiler->flag_decimal = 1;
    break;
  default:
    break;
  }
}

void
jit_compiler_compile_block(struct jit_compiler* p_compiler,
                           struct util_buffer* p_buf,
                           uint16_t start_addr_6502) {
  struct jit_opcode_details opcode_details = {};
  uint32_t block_max_cycles = 0;
  struct util_buffer* p_single_opcode_buf = p_compiler->p_single_opcode_buf;
  uint16_t addr_6502 = start_addr_6502;

  assert(!util_buffer_get_pos(p_buf));

  p_compiler->reg_a = k_value_unknown;
  p_compiler->reg_x = k_value_unknown;
  p_compiler->reg_y = k_value_unknown;
  p_compiler->flag_carry = k_value_unknown;
  p_compiler->flag_decimal = k_value_unknown;

  jit_invalidate_block_with_addr(p_compiler, addr_6502);

  /* Prepend space for countdown check. We can't fill it in yet because we
   * don't know how many cycles the block will be.
   */
  util_buffer_fill(p_buf, '\xcc', p_compiler->len_x64_countdown);

  while (1) {
    uint8_t single_opcode_buffer[128];
    uint8_t num_uops;
    uint8_t num_bytes_6502;
    uint8_t i;
    void* p_host_address;
    uint32_t jit_ptr;
    size_t buf_needed;
    int ends_block = 0;

    util_buffer_setup(p_single_opcode_buf,
                      &single_opcode_buffer[0],
                      sizeof(single_opcode_buffer));

    p_host_address = (util_buffer_get_base_address(p_buf) +
                      util_buffer_get_pos(p_buf));
    util_buffer_set_base_address(p_single_opcode_buf, p_host_address);

    num_uops = jit_compiler_get_opcode_details(p_compiler,
                                               &opcode_details,
                                               addr_6502);

    for (i = 0; i < num_uops; ++i) {
      jit_compiler_process_uop(p_compiler,
                               p_single_opcode_buf,
                               &opcode_details.uops[i]);
    }

    /* TODO: continue the block after conditional branches. */
    if (opcode_details.branches != k_bra_n) {
      ends_block = 1;
      if (opcode_details.branches == k_bra_m) {
        opcode_details.uops[0].opcode = 0x4C; /* JMP abs */
        opcode_details.uops[0].value1 =
            (int32_t) (size_t) p_compiler->get_block_host_address(
                p_compiler->p_host_address_object, (addr_6502 + 2));
        jit_compiler_process_uop(p_compiler,
                                 p_single_opcode_buf,
                                 &opcode_details.uops[0]);
      }
    }

    buf_needed = util_buffer_get_pos(p_single_opcode_buf);
    if (!ends_block) {
      buf_needed += p_compiler->len_x64_jmp;
    }

    if (util_buffer_remaining(p_buf) < buf_needed) {
      /* Not enough space; need to end this block with a jump to the next
       * address, effectively splitting the block.
       */
      util_buffer_set_pos(p_single_opcode_buf, 0);
      opcode_details.uops[0].opcode = 0x4C; /* JMP abs */
      opcode_details.uops[0].value1 =
          (int32_t) (size_t) p_compiler->get_block_host_address(
              p_compiler->p_host_address_object, addr_6502);
      jit_compiler_process_uop(p_compiler,
                               p_single_opcode_buf,
                               &opcode_details.uops[0]);
      opcode_details.len = 0;
      opcode_details.cycles = 0;
      ends_block = 1;
    }

    num_bytes_6502 = opcode_details.len;
    jit_ptr = (uint32_t) (size_t) p_host_address;
    for (i = 0; i < num_bytes_6502; ++i) {
      jit_invalidate_jump_target(p_compiler, addr_6502);
      p_compiler->p_jit_ptrs[addr_6502] = jit_ptr;
      addr_6502++;
    }

    block_max_cycles += opcode_details.cycles;

    util_buffer_append(p_buf, p_single_opcode_buf);

    if (ends_block) {
      break;
    }
  }

  /* Fill the unused portion of the buffer with 0xcc, i.e. int3.
   * There are a few good reasons for this:
   * 1) Clarity: see where a code block ends, especially if there was
   * previously a larger code block at this address.
   * 2) Bug detection: better chance of a clean crash if something does a bad
   * jump.
   * 3) Performance. int3 will stop the Intel instruction decoder.
   */
  util_buffer_fill_to_end(p_buf, '\xcc');

  /* Fill in block header countdown check. */
  util_buffer_set_pos(p_buf, 0);
  opcode_details.uops[0].opcode = k_opcode_countdown;
  /* TODO: have the 6502 address implied by the jump target. */
  opcode_details.uops[0].value1 = start_addr_6502;
  opcode_details.uops[0].value2 = block_max_cycles;
  opcode_details.uops[0].optype = -1;
  jit_compiler_process_uop(p_compiler, p_buf, &opcode_details.uops[0]);
}
