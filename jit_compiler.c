#include "jit_compiler.h"

#include "asm_x64_common.h"
#include "asm_x64_jit.h"
#include "defs_6502.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

struct jit_compiler {
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
  int branches;
  struct jit_uop uops[8];
};

static const int32_t k_value_unknown = -1;

enum {
  k_opcode_countdown = 0x100,
  k_opcode_debug,
  k_opcode_FLAGA,
  k_opcode_FLAGX,
  k_opcode_FLAGY,
  k_opcode_ADD_IMM,
  k_opcode_ADD_Y_SCRATCH,
  k_opcode_INC_SCRATCH,
  k_opcode_JMP_SCRATCH,
  k_opcode_LOAD_CARRY,
  k_opcode_LOAD_CARRY_INV,
  k_opcode_LOAD_OVERFLOW,
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
jit_compiler_create(uint8_t* p_mem_read,
                    void* (*get_block_host_address)(void*, uint16_t),
                    uint16_t (*get_jit_ptr_block)(void*, uint32_t),
                    void* p_host_address_object,
                    uint32_t* p_jit_ptrs,
                    int debug) {
  size_t i;

  struct jit_compiler* p_compiler = malloc(sizeof(struct jit_compiler));
  if (p_compiler == NULL) {
    errx(1, "cannot alloc jit_compiler");
  }
  (void) memset(p_compiler, '\0', sizeof(struct jit_compiler));

  p_compiler->p_mem_read = p_mem_read;
  p_compiler->get_block_host_address = get_block_host_address;
  p_compiler->get_jit_ptr_block = get_jit_ptr_block;
  p_compiler->p_host_address_object = p_host_address_object;
  p_compiler->p_jit_ptrs = p_jit_ptrs;
  p_compiler->debug = debug;

  p_compiler->p_single_opcode_buf = util_buffer_create();
  p_compiler->p_tmp_buf = util_buffer_create();

  p_compiler->no_code_jit_ptr = 
      (uint32_t) (size_t) get_block_host_address(p_host_address_object,
                                                 (k_6502_addr_space_size - 1));

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    jit_set_jit_ptr_no_code(p_compiler, i);
  }

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

  uint8_t* p_mem_read = p_compiler->p_mem_read;
  uint16_t addr_plus_1 = (addr_6502 + 1);
  uint16_t addr_plus_2 = (addr_6502 + 2);
  int jump_fixup = 0;
  int32_t main_value1 = -1;
  struct jit_uop* p_uop = &p_details->uops[0];

  opcode_6502 = p_mem_read[addr_6502];
  optype = g_optypes[opcode_6502];
  opmode = g_opmodes[opcode_6502];
  opmem = g_opmem[optype];

  p_details->opcode_6502 = opcode_6502;
  p_details->len = g_opmodelens[opmode];
  p_details->branches = g_opbranch[optype];

  if (p_compiler->debug) {
    p_uop->opcode = k_opcode_debug;
    p_uop->optype = -1;
    p_uop->value1 = addr_6502;
    p_uop++;
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

  /* Code invalidation for writes, aka. self-modifying code. */
  if (opmem == k_write || opmem == k_rw) {
    switch (opmode) {
    case k_abs:
      p_uop->opcode = k_opcode_WRITE_INV_ABS;
      p_uop->value1 = main_value1;
      p_uop->optype = -1;
      p_uop++;
    default:
      break;
    }
  }

  /* Pre-main uops. */
  switch (optype) {
  case k_adc:
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
jit_compiler_emit_uop(struct util_buffer* p_dest_buf,
                      struct jit_uop* p_uop) {
  int opcode = p_uop->opcode;
  int value1 = p_uop->value1;
  int value2 = p_uop->value2;

  switch (opcode) {
  case k_opcode_debug:
    asm_x64_emit_jit_call_debug(p_dest_buf, (uint16_t) value1);
    break;
  case k_opcode_ADD_Y_SCRATCH:
    asm_x64_emit_jit_ADD_Y_SCRATCH(p_dest_buf);
    break;
  case k_opcode_ADD_IMM:
    asm_x64_emit_jit_ADD_IMM(p_dest_buf, (uint8_t) value1);
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
  case 0x02:
    asm_x64_emit_instruction_EXIT(p_dest_buf);
    break;
  case 0x08:
    asm_x64_emit_instruction_PHP(p_dest_buf);
    break;
  case 0x0A:
    asm_x64_emit_jit_ASL_ACC(p_dest_buf);
    break;
  case 0x10:
    asm_x64_emit_jit_BPL(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x18:
    asm_x64_emit_instruction_CLC(p_dest_buf);
    break;
  case 0x24: /* BIT zpg */
  case 0x2C: /* BIT abs */
    asm_x64_emit_jit_BIT(p_dest_buf, (uint16_t) value1);
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
  case 0x38:
    asm_x64_emit_instruction_SEC(p_dest_buf);
    break;
  case 0x48:
    asm_x64_emit_instruction_PHA(p_dest_buf);
    break;
  case 0x4A:
    asm_x64_emit_jit_LSR_ACC(p_dest_buf);
    break;
  case 0x4C:
    asm_x64_emit_jit_JMP(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x50:
    asm_x64_emit_jit_BVC(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x58:
    asm_x64_emit_instruction_CLI(p_dest_buf);
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
  case 0x78:
    asm_x64_emit_instruction_SEI(p_dest_buf);
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
  case 0x88:
    asm_x64_emit_instruction_DEY(p_dest_buf);
    break;
  case 0x90:
    asm_x64_emit_jit_BCC(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0x91: /* STA idy */
    asm_x64_emit_jit_STA_scratch(p_dest_buf);
    break;
  case 0x9A:
    asm_x64_emit_instruction_TXS(p_dest_buf);
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
  case 0xA6:
    /* Actually LDX zpg but re-using LDX abs code. */
    asm_x64_emit_jit_LDX_ABS(p_dest_buf, (uint8_t) value1);
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
  case 0xAD:
    asm_x64_emit_jit_LDA_ABS(p_dest_buf, (uint16_t) value1);
    break;
  case 0xB0:
    asm_x64_emit_jit_BCS(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xB6: /* LDX zpy */
    asm_x64_emit_jit_LDX_scratch(p_dest_buf);
    break;
  case 0xB8:
    asm_x64_emit_instruction_CLV(p_dest_buf);
    break;
  case 0xBA:
    asm_x64_emit_instruction_TSX(p_dest_buf);
    break;
  case 0xBD:
    asm_x64_emit_jit_LDA_ABX(p_dest_buf, (uint16_t) value1);
    break;
  case 0xC0:
    asm_x64_emit_jit_CPY_IMM(p_dest_buf, (uint8_t) value1);
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
  case 0xD0:
    asm_x64_emit_jit_BNE(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xD8:
    asm_x64_emit_instruction_CLD(p_dest_buf);
    break;
  case 0xE0:
    asm_x64_emit_jit_CPX_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xE6:
    asm_x64_emit_jit_INC_ZPG(p_dest_buf, (uint8_t) value1);
    break;
  case 0xE8:
    asm_x64_emit_instruction_INX(p_dest_buf);
    break;
  case 0xE9:
    asm_x64_emit_jit_SBC_IMM(p_dest_buf, (uint8_t) value1);
    break;
  case 0xF0:
    asm_x64_emit_jit_BEQ(p_dest_buf, (void*) (size_t) value1);
    break;
  case 0xF2:
    asm_x64_emit_instruction_CRASH(p_dest_buf);
    break;
  case 0xF8:
    asm_x64_emit_instruction_SED(p_dest_buf);
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
    opreg = g_optype_sets_register[optype];
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

  jit_compiler_emit_uop(p_dest_buf, p_uop);

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
                           uint16_t addr_6502) {
  struct jit_opcode_details opcode_details = {};

  struct util_buffer* p_single_opcode_buf = p_compiler->p_single_opcode_buf;

  p_compiler->reg_a = k_value_unknown;
  p_compiler->reg_x = k_value_unknown;
  p_compiler->reg_y = k_value_unknown;
  p_compiler->flag_carry = k_value_unknown;
  p_compiler->flag_decimal = k_value_unknown;

  jit_invalidate_block_with_addr(p_compiler, addr_6502);

  while (1) {
    uint8_t single_opcode_buffer[128];
    uint8_t num_uops;
    uint8_t num_bytes_6502;
    uint8_t i;
    void* p_host_address;
    uint32_t jit_ptr;

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

    num_bytes_6502 = opcode_details.len;
    jit_ptr = (uint32_t) (size_t) p_host_address;
    for (i = 0; i < num_bytes_6502; ++i) {
      jit_invalidate_jump_target(p_compiler, addr_6502);
      p_compiler->p_jit_ptrs[addr_6502] = jit_ptr;
      addr_6502++;
    }

    util_buffer_append(p_buf, p_single_opcode_buf);

    if (opcode_details.branches == k_bra_y) {
      break;
    }
  }
}
