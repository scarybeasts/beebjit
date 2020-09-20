#include "defs_6502.h"

#include <assert.h>

const char* g_p_opnames[k_6502_op_num_types] =
{
  "!!!", "???", "BRK", "ORA", "ASL", "PHP", "BPL", "CLC",
  "JSR", "AND", "BIT", "PLP", "ROL", "BMI", "SEC", "RTI",
  "EOR", "LSR", "PHA", "JMP", "BVC", "CLI", "RTS", "ADC",
  "PLA", "ROR", "BVS", "SEI", "STY", "STA", "STX", "DEY",
  "TXA", "BCC", "TYA", "TXS", "LDY", "LDA", "LDX", "TAY",
  "TAX", "BCS", "CLV", "TSX", "CPY", "CMP", "CPX", "DEC",
  "INY", "DEX", "BNE", "CLD", "SBC", "INX", "NOP", "INC",
  "BEQ", "SED", "SAX", "ALR", "SLO", "SHY", "ANC", "LAX",
  "DCP", "SRE", "RLA", "AHX", "XAA", "RRA", "AXS", "TSB",
  "TRB", "STZ", "BRA", "PHX", "PHY", "PLX", "PLY",
};

uint8_t g_opmem[k_6502_op_num_types] = {
  k_nomem, k_nomem, k_nomem, k_read , k_rw   , k_nomem, k_nomem, k_nomem,
  k_nomem, k_read , k_read , k_nomem, k_rw   , k_nomem, k_nomem, k_nomem,
  k_read , k_rw   , k_nomem, k_nomem, k_nomem, k_nomem, k_nomem, k_read ,
  k_nomem, k_rw   , k_nomem, k_nomem, k_write, k_write, k_write, k_nomem,
  k_nomem, k_nomem, k_nomem, k_nomem, k_read , k_read , k_read , k_nomem,
  k_nomem, k_nomem, k_nomem, k_nomem, k_read , k_read , k_read , k_rw   ,
  k_nomem, k_nomem, k_nomem, k_nomem, k_read , k_nomem, k_read , k_rw   ,
  k_nomem, k_nomem, k_write, k_nomem, k_rw   , k_write, k_nomem, k_read ,
  k_rw   , k_rw   , k_rw   , k_write, k_nomem, k_rw   , k_nomem, k_rw   ,
  k_rw   , k_write, k_nomem, k_nomem, k_nomem, k_nomem, k_nomem,
};

uint8_t g_opbranch[k_6502_op_num_types] = {
  k_bra_y, k_bra_y, k_bra_y, k_bra_n, k_bra_n, k_bra_n, k_bra_m, k_bra_n,
  k_bra_y, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_m, k_bra_n, k_bra_y,
  k_bra_n, k_bra_n, k_bra_n, k_bra_y, k_bra_m, k_bra_n, k_bra_y, k_bra_n,
  k_bra_n, k_bra_n, k_bra_m, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
  k_bra_n, k_bra_m, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
  k_bra_n, k_bra_m, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
  k_bra_n, k_bra_n, k_bra_m, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
  k_bra_m, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
  k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
  k_bra_n, k_bra_n, k_bra_y, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
};

uint8_t g_optype_uses_carry[k_6502_op_num_types] = {
  0, 0, 0, 0, 0, 1, 0, 0, /* PHP */
  0, 0, 0, 0, 1, 0, 0, 0, /* ROL */
  0, 0, 0, 0, 0, 0, 0, 1, /* ADC */
  0, 1, 0, 0, 0, 0, 0, 0, /* ROR */
  0, 1, 0, 0, 0, 0, 0, 0, /* BCC */
  0, 1, 0, 0, 0, 0, 0, 0, /* BCS */
  0, 0, 0, 0, 1, 0, 0, 0, /* SBC */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 0, 0, 0, 0, 0, /* RLA */
  0, 0, 0, 0, 0, 0, 0,
};

uint8_t g_optype_changes_carry[k_6502_op_num_types] = {
  0, 0, 0, 0, 1, 0, 0, 1, /* ASL, CLC */
  0, 0, 0, 1, 1, 0, 1, 1, /* PLP, ROL, SEC, RTI */
  0, 1, 0, 0, 0, 0, 0, 1, /* LSR, ADC */
  0, 1, 0, 0, 0, 0, 0, 0, /* ROR */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 1, 1, 1, 0, /* CPY, CMP, CPX */
  0, 0, 0, 0, 1, 0, 0, 0, /* SBC */
  0, 0, 0, 1, 1, 0, 1, 0, /* ALR, SLO, ANC */
  1, 1, 1, 0, 0, 1, 1, 0, /* DCP, SRE, RLA, RRA, AXS */
  0, 0, 0, 0, 0, 0, 0,
};

uint8_t g_optype_changes_overflow[k_6502_op_num_types] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 1, 0, 0, 0, 1, /* BIT, PLP, RTI */
  0, 0, 0, 0, 0, 0, 0, 1, /* ADC */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 0, 0, 0, 0, 0, /* CLV */
  0, 0, 0, 0, 1, 0, 0, 0, /* SBC */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 1, 0, 0, /* RRA */
  0, 0, 0, 0, 0, 0, 0,
};

/* TODO: need k_ax for LAX?? */
uint8_t g_optype_sets_register[k_6502_op_num_types] =
{
  0  , 0  , 0  , k_a, 0  , 0  , 0  , 0  , /* ORA */
  0  , k_a, 0  , 0  , 0  , 0  , 0  , 0  , /* AND */
  k_a, 0  , 0  , 0  , 0  , 0  , 0  , k_a, /* EOR, ADC */
  k_a, 0  , 0  , 0  , 0  , 0  , 0  , k_y, /* PLA, DEY */
  k_a, 0  , k_a, 0  , k_y, k_a, k_x, k_y, /* TXA, TYA, LDY, LDA, LDX, TAY */
  k_x, 0  , 0  , k_x, 0  , 0  , 0  , 0  , /* TAX, TSX */
  k_y, k_x, 0  , 0  , k_a, k_x, 0  , 0  , /* INY, DEX, SBC, INX */
  0  , 0  , 0  , k_a, k_a, 0  , k_a, k_a, /* ALR, SLO, ANC, LAX */
  0  , k_a, k_a, 0  , k_a, k_a, k_x, 0  , /* SRE, RLA, XAA, RRA, AXS */
  0  , 0  , 0  , 0  , 0  , k_x, k_y,      /* PLX, PLY */
};

/* NOTE: TSB, TRB only change Z. */
uint8_t g_optype_changes_nz_flags[k_6502_op_num_types] =
{
  0, 0, 0, 1, 1, 0, 0, 0, /* ORA, ASL */
  0, 1, 1, 1, 1, 0, 0, 1, /* AND, BIT, PLP, ROL, RTI */
  1, 1, 0, 0, 0, 0, 0, 1, /* EOR, LSR, ADC */
  1, 1, 0, 0, 0, 0, 0, 1, /* PLA, ROR, DEY */
  1, 0, 1, 0, 1, 1, 1, 1, /* TXA, TYA, LDY, LDA, LDX, TAY */
  1, 0, 0, 1, 1, 1, 1, 1, /* TAX, TSX, CPY, CMP, CPX, DEC */
  1, 1, 0, 0, 1, 1, 0, 1, /* INY, DEX, SBC, INX, INC */
  0, 0, 0, 1, 1, 0, 1, 1, /* ALR, SLO, ANC, LAX */
  1, 1, 1, 0, 1, 1, 1, 1, /* DCP, SRE, RLA, XAA, RRA, AXS, TSB */
  1, 0, 0, 0, 0, 1, 1,    /* TRB, PLX, PLY */
};

uint8_t s_optypes_6502[k_6502_op_num_opcodes] =
{
  /* 0x00 */
  k_brk, k_ora, k_kil, k_slo, k_nop, k_ora, k_asl, k_slo,
  k_php, k_ora, k_asl, k_anc, k_nop, k_ora, k_asl, k_slo,
  /* 0x10 */
  k_bpl, k_ora, k_kil, k_unk, k_nop, k_ora, k_asl, k_unk,
  k_clc, k_ora, k_nop, k_unk, k_nop, k_ora, k_asl, k_unk,
  /* 0x20 */
  k_jsr, k_and, k_kil, k_rla, k_bit, k_and, k_rol, k_unk,
  k_plp, k_and, k_rol, k_unk, k_bit, k_and, k_rol, k_rla,
  /* 0x30 */
  k_bmi, k_and, k_kil, k_rla, k_nop, k_and, k_rol, k_unk,
  k_sec, k_and, k_nop, k_unk, k_nop, k_and, k_rol, k_unk,
  /* 0x40 */
  k_rti, k_eor, k_kil, k_unk, k_nop, k_eor, k_lsr, k_sre,
  k_pha, k_eor, k_lsr, k_alr, k_jmp, k_eor, k_lsr, k_sre,
  /* 0x50 */
  k_bvc, k_eor, k_kil, k_unk, k_nop, k_eor, k_lsr, k_unk,
  k_cli, k_eor, k_nop, k_unk, k_nop, k_eor, k_lsr, k_unk,
  /* 0x60 */
  k_rts, k_adc, k_kil, k_unk, k_nop, k_adc, k_ror, k_unk,
  k_pla, k_adc, k_ror, k_unk, k_jmp, k_adc, k_ror, k_rra,
  /* 0x70 */
  k_bvs, k_adc, k_kil, k_rra, k_nop, k_adc, k_ror, k_unk,
  k_sei, k_adc, k_nop, k_unk, k_nop, k_adc, k_ror, k_unk,
  /* 0x80 */
  k_nop, k_sta, k_nop, k_sax, k_sty, k_sta, k_stx, k_sax,
  k_dey, k_nop, k_txa, k_xaa, k_sty, k_sta, k_stx, k_sax,
  /* 0x90 */
  k_bcc, k_sta, k_kil, k_ahx, k_sty, k_sta, k_stx, k_unk,
  k_tya, k_sta, k_txs, k_unk, k_shy, k_sta, k_unk, k_unk,
  /* 0xa0 */
  k_ldy, k_lda, k_ldx, k_unk, k_ldy, k_lda, k_ldx, k_lax,
  k_tay, k_lda, k_tax, k_lax, k_ldy, k_lda, k_ldx, k_lax,
  /* 0xb0 */
  k_bcs, k_lda, k_kil, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  k_clv, k_lda, k_tsx, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  /* 0xc0 */
  k_cpy, k_cmp, k_nop, k_unk, k_cpy, k_cmp, k_dec, k_unk,
  k_iny, k_cmp, k_dex, k_axs, k_cpy, k_cmp, k_dec, k_unk,
  /* 0xd0 */
  k_bne, k_cmp, k_kil, k_dcp, k_nop, k_cmp, k_dec, k_unk,
  k_cld, k_cmp, k_nop, k_unk, k_nop, k_cmp, k_dec, k_unk,
  /* 0xe0 */
  k_cpx, k_sbc, k_nop, k_unk, k_cpx, k_sbc, k_inc, k_unk,
  k_inx, k_sbc, k_nop, k_sbc, k_cpx, k_sbc, k_inc, k_unk,
  /* 0xf0 */
  k_beq, k_sbc, k_kil, k_unk, k_nop, k_sbc, k_inc, k_unk,
  k_sed, k_sbc, k_nop, k_unk, k_nop, k_sbc, k_inc, k_unk,
};

uint8_t s_opmodes_6502[k_6502_op_num_opcodes] =
{
  /* 0x00 */
  k_imm, k_idx, 0    , k_idx, k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_acc, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0x10 */
  k_rel, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpx, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, k_abx, 0    ,
  /* 0x20 */
  k_abs, k_idx, 0    , k_idx, k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_acc, 0    , k_abs, k_abs, k_abs, k_abs,
  /* 0x30 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpx, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, k_abx, 0    ,
  /* 0x40 */
  k_nil, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_acc, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0x50 */
  k_rel, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpx, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, k_abx, 0    ,
  /* 0x60 */
  k_nil, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_acc, 0    , k_ind, k_abs, k_abs, k_abs,
  /* 0x70 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpx, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, k_abx, 0    ,
  /* 0x80 */
  k_imm, k_idx, k_imm, k_idx, k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_nil, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0x90 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpy, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, 0    , 0    ,
  /* 0xa0 */
  k_imm, k_idx, k_imm, 0    , k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_nil, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0xb0 */
  k_rel, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpy, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, k_aby, 0    ,
  /* 0xc0 */
  k_imm, k_idx, k_imm, 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, k_imm, k_abs, k_abs, k_abs, 0    ,
  /* 0xd0 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpx, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, k_abx, 0    ,
  /* 0xe0 */
  k_imm, k_idx, k_imm, 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, k_imm, k_abs, k_abs, k_abs, 0    ,
  /* 0xf0 */
  k_rel, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpx, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, k_abx, 0    ,
};

uint8_t s_opcycles_6502[k_6502_op_num_opcodes] =
{
  /* 0x00 */
  7, 6, 1, 8, 3, 3, 5, 5,
  3, 2, 2, 2, 4, 4, 6, 6,
  /* 0x10 */
  2, 5, 1, 0, 4, 4, 6, 0,
  2, 4, 2, 0, 4, 4, 7, 0,
  /* 0x20 */
  6, 6, 1, 8, 3, 3, 5, 0,
  4, 2, 2, 0, 4, 4, 6, 6,
  /* 0x30 */
  2, 5, 0, 8, 4, 4, 6, 0,
  2, 4, 2, 0, 4, 4, 7, 0,
  /* 0x40 */
  6, 6, 0, 0, 3, 3, 5, 5,
  3, 2, 2, 2, 3, 4, 6, 6,
  /* 0x50 */
  2, 5, 0, 0, 4, 4, 6, 0,
  2, 4, 2, 0, 4, 4, 7, 0,
  /* 0x60 */
  6, 6, 0, 0, 3, 3, 5, 0,
  4, 2, 2, 0, 5, 4, 6, 6,
  /* 0x70 */
  2, 5, 0, 8, 4, 4, 6, 0,
  2, 4, 2, 0, 4, 4, 7, 0,
  /* 0x80 */
  2, 6, 2, 6, 3, 3, 3, 3,
  2, 2, 2, 2, 4, 4, 4, 4,
  /* 0x90 */
  2, 6, 0, 6, 4, 4, 4, 0,
  2, 5, 2, 0, 5, 5, 0, 0,
  /* 0xa0 */
  2, 6, 2, 0, 3, 3, 3, 3,
  2, 2, 2, 2, 4, 4, 4, 4,
  /* 0xb0 */
  2, 5, 0, 0, 4, 4, 4, 0,
  2, 4, 2, 0, 4, 4, 4, 0,
  /* 0xc0 */
  2, 6, 2, 0, 3, 3, 5, 0,
  2, 2, 2, 2, 4, 4, 6, 0,
  /* 0xd0 */
  2, 5, 0, 8, 4, 4, 6, 0,
  2, 4, 2, 0, 4, 4, 7, 0,
  /* 0xe0 */
  2, 6, 2, 0, 3, 3, 5, 0,
  2, 2, 2, 2, 4, 4, 6, 0,
  /* 0xf0 */
  2, 5, 1, 0, 4, 4, 6, 0,
  2, 4, 2, 0, 4, 4, 7, 0,
};

uint8_t s_optypes_65c12[k_6502_op_num_opcodes];
uint8_t s_opmodes_65c12[k_6502_op_num_opcodes];
uint8_t s_opcycles_65c12[k_6502_op_num_opcodes];

uint8_t g_opmodelens[k_6502_op_num_modes] =
{
  1, /* ??? */
  1, /* nil */
  1, /* acc */
  2, /* imm */
  2, /* zpg */
  3, /* abs */
  2, /* zpx */
  2, /* zpy */
  3, /* abx */
  3, /* aby */
  2, /* idx */
  2, /* idy */
  3, /* ind */
  2, /* rel */
};

static int s_inited;

uint8_t*
defs_6502_get_6502_optype_map() {
  return s_optypes_6502;
}

uint8_t*
defs_6502_get_6502_opmode_map() {
  return s_opmodes_6502;
}

uint8_t*
defs_6502_get_6502_opcycles_map() {
  return s_opcycles_6502;
}

uint8_t*
defs_6502_get_65c12_optype_map() {
  return s_optypes_65c12;
}

uint8_t*
defs_6502_get_65c12_opmode_map() {
  return s_opmodes_65c12;
}

uint8_t*
defs_6502_get_65c12_opcycles_map() {
  return s_opcycles_65c12;
}

static void
defs_65c12_set_opcode(uint8_t opcode, uint8_t type, uint8_t mode) {
  uint8_t cycles = 0;
  int is_rmw = (g_opmem[type] == k_rw);

  switch (mode) {
  case k_nil:
    switch (type) {
    case k_phx:
    case k_phy:
      cycles = 3;
      break;
    case k_plx:
    case k_ply:
      cycles = 4;
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_zpg:
    cycles = (3 + (is_rmw * 2));
    break;
  case k_abs:
  case k_zpx:
    cycles = (4 + (is_rmw * 2));
    break;
  case k_abx:
    cycles = 4;
    if (g_opmem[type] == k_write) {
      cycles++;
    }
    break;
  case k_acc:
  case k_imm:
  case k_rel:
    cycles = 2;
    break;
  case k_iax:
    cycles = 6;
    break;
  case k_id:
    cycles = 5;
    break;
  default:
    assert(0);
  }
  assert(s_opcycles_65c12[opcode] == 0);
  s_optypes_65c12[opcode] = type;
  s_opmodes_65c12[opcode] = mode;
  s_opcycles_65c12[opcode] = cycles;
}

void
defs_6502_init() {
  uint32_t i;

  if (s_inited) {
    return;
  }
  s_inited = 1;

  /* 65c12 setup. */
  /* Copy across common opcodes. */
  for (i = 0; i < k_6502_op_num_opcodes; ++i) {
    uint8_t optype = s_optypes_6502[i];
    s_optypes_65c12[i] = k_unk;
    if (optype == k_unk) {
      continue;
    }
    if (optype == k_kil) {
      continue;
    }
    if (optype > k_last_6502_documented) {
      continue;
    }
    if ((optype == k_nop) && (i != 0xEA)) {
      continue;
    }
    s_optypes_65c12[i] = optype;
    s_opmodes_65c12[i] = s_opmodes_6502[i];
    s_opcycles_65c12[i] = s_opcycles_6502[i];
  }
  /* Set up 65c12 specific opcodes. */
  defs_65c12_set_opcode(0x04, k_tsb, k_zpg);
  defs_65c12_set_opcode(0x0C, k_tsb, k_abs);
  defs_65c12_set_opcode(0x14, k_trb, k_zpg);
  defs_65c12_set_opcode(0x1A, k_inc, k_acc);
  defs_65c12_set_opcode(0x1C, k_trb, k_abs);
  defs_65c12_set_opcode(0x3A, k_dec, k_acc);
  defs_65c12_set_opcode(0x3C, k_bit, k_abx);
  defs_65c12_set_opcode(0x52, k_eor, k_id);
  defs_65c12_set_opcode(0x5A, k_phy, k_nil);
  defs_65c12_set_opcode(0x64, k_stz, k_zpg);
  defs_65c12_set_opcode(0x74, k_stz, k_zpx);
  defs_65c12_set_opcode(0x7A, k_ply, k_nil);
  defs_65c12_set_opcode(0x7C, k_jmp, k_iax);
  defs_65c12_set_opcode(0x80, k_bra, k_rel);
  defs_65c12_set_opcode(0x89, k_bit, k_imm);
  defs_65c12_set_opcode(0x92, k_sta, k_id);
  defs_65c12_set_opcode(0x9C, k_stz, k_abs);
  defs_65c12_set_opcode(0x9E, k_stz, k_abx);
  defs_65c12_set_opcode(0xB2, k_lda, k_id);
  defs_65c12_set_opcode(0xD2, k_cmp, k_id);
  defs_65c12_set_opcode(0xDA, k_phx, k_nil);
  defs_65c12_set_opcode(0xFA, k_plx, k_nil);
}
