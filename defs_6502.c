#include "defs_6502.h"

#include <assert.h>
#include <stddef.h>

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
  "DCP", "SRE", "RLA", "AHX", "XAA", "RRA", "AXS", "ISC",
  "ARR", "TAS", "LAS", "SHX", NULL , NULL , NULL , NULL ,
  "TSB", "TRB", "STZ", "BRA", "PHX", "PHY", "PLX", "PLY",
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
  k_bra_n, k_bra_n, k_bra_n, k_bra_n, 0      , 0      , 0      , 0      ,
  k_bra_n, k_bra_n, k_bra_n, k_bra_y, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
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
  1, 0, 0, 0, 0, 0, 0, 0, /* ARR */
  0, 0, 0, 0, 0, 0, 0, 0,
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
  1, 1, 1, 0, 0, 1, 1, 1, /* DCP, SRE, RLA, RRA, AXS, ISC */
  1, 0, 0, 0, 0, 0, 0, 0, /* ARR */
  0, 0, 0, 0, 0, 0, 0, 0,
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
  0, 0, 0, 0, 0, 1, 0, 1, /* RRA, ISC */
  1, 0, 0, 0, 0, 0, 0, 0, /* ARR */
  0, 0, 0, 0, 0, 0, 0, 0,
};

/* TODO: need k_ax for LAX, k_axs for LAS?? */
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
  0  , k_a, k_a, 0  , k_a, k_a, k_x, k_a, /* SRE, RLA, XAA, RRA, AXS, ISC */
  k_a, 0  , k_a, 0  , 0  , 0  , 0  , 0  , /* ARR, LAS */
  0  , 0  , 0  , 0  , 0  , 0  , k_x, k_y, /* PLX, PLY */
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
  1, 1, 1, 0, 1, 1, 1, 1, /* DCP, SRE, RLA, XAA, RRA, AXS, ISC */
  1, 1, 0, 0, 0, 0, 0, 0, /* ARR, TAS */
  1, 1, 0, 0, 0, 0, 1, 1, /* TSB, TRB, PLX, PLY */
};

uint8_t s_optypes_6502[k_6502_op_num_opcodes] =
{
  /* 0x00 */
  k_brk, k_ora, k_kil, k_slo, k_nop, k_ora, k_asl, k_slo,
  k_php, k_ora, k_asl, k_anc, k_nop, k_ora, k_asl, k_slo,
  /* 0x10 */
  k_bpl, k_ora, k_kil, k_slo, k_nop, k_ora, k_asl, k_slo,
  k_clc, k_ora, k_nop, k_slo, k_nop, k_ora, k_asl, k_slo,
  /* 0x20 */
  k_jsr, k_and, k_kil, k_rla, k_bit, k_and, k_rol, k_rla,
  k_plp, k_and, k_rol, k_anc, k_bit, k_and, k_rol, k_rla,
  /* 0x30 */
  k_bmi, k_and, k_kil, k_rla, k_nop, k_and, k_rol, k_rla,
  k_sec, k_and, k_nop, k_rla, k_nop, k_and, k_rol, k_rla,
  /* 0x40 */
  k_rti, k_eor, k_kil, k_sre, k_nop, k_eor, k_lsr, k_sre,
  k_pha, k_eor, k_lsr, k_alr, k_jmp, k_eor, k_lsr, k_sre,
  /* 0x50 */
  k_bvc, k_eor, k_kil, k_sre, k_nop, k_eor, k_lsr, k_sre,
  k_cli, k_eor, k_nop, k_sre, k_nop, k_eor, k_lsr, k_sre,
  /* 0x60 */
  k_rts, k_adc, k_kil, k_rra, k_nop, k_adc, k_ror, k_rra,
  k_pla, k_adc, k_ror, k_arr, k_jmp, k_adc, k_ror, k_rra,
  /* 0x70 */
  k_bvs, k_adc, k_kil, k_rra, k_nop, k_adc, k_ror, k_rra,
  k_sei, k_adc, k_nop, k_rra, k_nop, k_adc, k_ror, k_rra,
  /* 0x80 */
  k_nop, k_sta, k_nop, k_sax, k_sty, k_sta, k_stx, k_sax,
  k_dey, k_nop, k_txa, k_xaa, k_sty, k_sta, k_stx, k_sax,
  /* 0x90 */
  k_bcc, k_sta, k_kil, k_ahx, k_sty, k_sta, k_stx, k_sax,
  k_tya, k_sta, k_txs, k_tas, k_shy, k_sta, k_shx, k_ahx,
  /* 0xa0 */
  k_ldy, k_lda, k_ldx, k_lax, k_ldy, k_lda, k_ldx, k_lax,
  k_tay, k_lda, k_tax, k_lax, k_ldy, k_lda, k_ldx, k_lax,
  /* 0xb0 */
  k_bcs, k_lda, k_kil, k_lax, k_ldy, k_lda, k_ldx, k_lax,
  k_clv, k_lda, k_tsx, k_las, k_ldy, k_lda, k_ldx, k_lax,
  /* 0xc0 */
  k_cpy, k_cmp, k_nop, k_dcp, k_cpy, k_cmp, k_dec, k_dcp,
  k_iny, k_cmp, k_dex, k_axs, k_cpy, k_cmp, k_dec, k_dcp,
  /* 0xd0 */
  k_bne, k_cmp, k_kil, k_dcp, k_nop, k_cmp, k_dec, k_dcp,
  k_cld, k_cmp, k_nop, k_dcp, k_nop, k_cmp, k_dec, k_dcp,
  /* 0xe0 */
  k_cpx, k_sbc, k_nop, k_isc, k_cpx, k_sbc, k_inc, k_isc,
  k_inx, k_sbc, k_nop, k_sbc, k_cpx, k_sbc, k_inc, k_isc,
  /* 0xf0 */
  k_beq, k_sbc, k_kil, k_isc, k_nop, k_sbc, k_inc, k_isc,
  k_sed, k_sbc, k_nop, k_isc, k_nop, k_sbc, k_inc, k_isc,
};

uint8_t s_opmodes_6502[k_6502_op_num_opcodes] =
{
  /* 0x00 */
  k_imm, k_idx, 0    , k_idx, k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_acc, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0x10 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpx, k_zpx,
  k_nil, k_aby, k_nil, k_aby, k_abx, k_abx, k_abx, k_abx,
  /* 0x20 */
  k_abs, k_idx, 0    , k_idx, k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_acc, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0x30 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpx, k_zpx,
  k_nil, k_aby, k_nil, k_aby, k_abx, k_abx, k_abx, k_abx,
  /* 0x40 */
  k_nil, k_idx, 0    , k_idx, k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_acc, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0x50 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpx, k_zpx,
  k_nil, k_aby, k_nil, k_aby, k_abx, k_abx, k_abx, k_abx,
  /* 0x60 */
  k_nil, k_idx, 0    , k_idx, k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_acc, k_imm, k_ind, k_abs, k_abs, k_abs,
  /* 0x70 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpx, k_zpx,
  k_nil, k_aby, k_nil, k_aby, k_abx, k_abx, k_abx, k_abx,
  /* 0x80 */
  k_imm, k_idx, k_imm, k_idx, k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_nil, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0x90 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpy, k_zpy,
  k_nil, k_aby, k_nil, k_aby, k_abx, k_abx, k_aby, k_aby,
  /* 0xa0 */
  k_imm, k_idx, k_imm, k_idx, k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_nil, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0xb0 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpy, k_zpy,
  k_nil, k_aby, k_nil, k_aby, k_abx, k_abx, k_aby, k_aby,
  /* 0xc0 */
  k_imm, k_idx, k_imm, k_idx, k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_nil, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0xd0 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpx, k_zpx,
  k_nil, k_aby, k_nil, k_aby, k_abx, k_abx, k_abx, k_abx,
  /* 0xe0 */
  k_imm, k_idx, k_imm, k_idx, k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_nil, k_imm, k_abs, k_abs, k_abs, k_abs,
  /* 0xf0 */
  k_rel, k_idy, 0    , k_idy, k_zpx, k_zpx, k_zpx, k_zpx,
  k_nil, k_aby, k_nil, k_aby, k_abx, k_abx, k_abx, k_abx,
};

uint8_t s_opcycles_6502[k_6502_op_num_opcodes] =
{
  /* 0x00 */
  7, 6, 0, 8, 3, 3, 5, 5,
  3, 2, 2, 2, 4, 4, 6, 6,
  /* 0x10 */
  2, 5, 0, 8, 4, 4, 6, 6,
  2, 4, 2, 7, 4, 4, 7, 7,
  /* 0x20 */
  6, 6, 0, 8, 3, 3, 5, 5,
  4, 2, 2, 2, 4, 4, 6, 6,
  /* 0x30 */
  2, 5, 0, 8, 4, 4, 6, 6,
  2, 4, 2, 7, 4, 4, 7, 7,
  /* 0x40 */
  6, 6, 0, 8, 3, 3, 5, 5,
  3, 2, 2, 2, 3, 4, 6, 6,
  /* 0x50 */
  2, 5, 0, 8, 4, 4, 6, 6,
  2, 4, 2, 7, 4, 4, 7, 7,
  /* 0x60 */
  6, 6, 0, 8, 3, 3, 5, 5,
  4, 2, 2, 2, 5, 4, 6, 6,
  /* 0x70 */
  2, 5, 0, 8, 4, 4, 6, 6,
  2, 4, 2, 7, 4, 4, 7, 7,
  /* 0x80 */
  2, 6, 2, 6, 3, 3, 3, 3,
  2, 2, 2, 2, 4, 4, 4, 4,
  /* 0x90 */
  2, 6, 0, 6, 4, 4, 4, 4,
  2, 5, 2, 5, 5, 5, 5, 5,
  /* 0xa0 */
  2, 6, 2, 6, 3, 3, 3, 3,
  2, 2, 2, 2, 4, 4, 4, 4,
  /* 0xb0 */
  2, 5, 0, 5, 4, 4, 4, 4,
  2, 4, 2, 4, 4, 4, 4, 4,
  /* 0xc0 */
  2, 6, 2, 8, 3, 3, 5, 5,
  2, 2, 2, 2, 4, 4, 6, 6,
  /* 0xd0 */
  2, 5, 0, 8, 4, 4, 6, 6,
  2, 4, 2, 7, 4, 4, 7, 7,
  /* 0xe0 */
  2, 6, 2, 8, 3, 3, 5, 5,
  2, 2, 2, 2, 4, 4, 6, 6,
  /* 0xf0 */
  2, 5, 0, 8, 4, 4, 6, 6,
  2, 4, 2, 7, 4, 4, 7, 7,
};

uint8_t s_opmem_6502[k_6502_op_num_opcodes];

uint8_t s_optypes_65c12[k_6502_op_num_opcodes];
uint8_t s_opmodes_65c12[k_6502_op_num_opcodes];
uint8_t s_opmem_65c12[k_6502_op_num_opcodes];
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
  3, /* iax */
  2, /* id */
  1, /* nil1 */
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
defs_6502_get_6502_opmem_map() {
  return s_opmem_6502;
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
defs_6502_get_65c12_opmem_map() {
  return s_opmem_65c12;
}

uint8_t*
defs_6502_get_65c12_opcycles_map() {
  return s_opcycles_65c12;
}

static uint8_t
defs_6502_calculate_opmem(uint8_t optype, uint8_t opmode) {
  uint8_t opmem = 0;
  switch (opmode) {
  case k_zpg:
  case k_abs:
  case k_zpx:
  case k_zpy:
  case k_abx:
  case k_aby:
  case k_idx:
  case k_idy:
  case k_ind:
  case k_iax:
  case k_id:
    switch (optype) {
    case k_sta:
    case k_stx:
    case k_sty:
    case k_sax:
    case k_shy:
    case k_ahx:
    case k_tas:
    case k_shx:
    case k_stz:
      opmem = k_opmem_write_flag;
      break;
    case k_asl:
    case k_rol:
    case k_lsr:
    case k_ror:
    case k_inc:
    case k_dec:
    case k_slo:
    case k_dcp:
    case k_sre:
    case k_rla:
    case k_rra:
    case k_isc:
    case k_tsb:
    case k_trb:
      opmem = (k_opmem_read_flag | k_opmem_write_flag);
      break;
    default:
      opmem = k_opmem_read_flag;
      break;
    }
    break;
  default:
    break;
  }
  return opmem;
}

static void
defs_6502_poplate_opmem_table(uint8_t* p_opmem,
                              uint8_t* p_optypes,
                              uint8_t* p_opmodes) {
  uint32_t i;
  for (i = 0; i < k_6502_op_num_opcodes; ++i) {
    uint8_t optype = p_optypes[i];
    uint8_t opmode = p_opmodes[i];
    uint8_t opmem = defs_6502_calculate_opmem(optype, opmode);
    p_opmem[i] = opmem;
  }
}

static void
defs_65c12_set_opcode(uint8_t opcode, uint8_t type, uint8_t mode) {
  uint8_t cycles = 0;
  uint8_t opmem = defs_6502_calculate_opmem(type, mode);
  int is_rmw = (opmem == (k_opmem_read_flag | k_opmem_write_flag));

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
    if (opmem & k_opmem_write_flag) {
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
  case k_nil1:
    cycles = 1;
    break;
  default:
    assert(0);
  }
  assert(s_opcycles_65c12[opcode] == 0);
  s_optypes_65c12[opcode] = type;
  s_opmodes_65c12[opcode] = mode;
  s_opcycles_65c12[opcode] = cycles;
}

static void
defs_6502_setup_6502(void) {
  defs_6502_poplate_opmem_table(&s_opmem_6502[0],
                                &s_optypes_6502[0],
                                &s_opmodes_6502[0]);
}

static void
defs_6502_setup_65c12(void) {
  uint32_t i;
  /* Copy across common opcodes from 6502. */
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
    /* SBC imm undocumented alias. */
    if (i == 0xEB) {
      continue;
    }
    s_optypes_65c12[i] = optype;
    s_opmodes_65c12[i] = s_opmodes_6502[i];
    s_opcycles_65c12[i] = s_opcycles_6502[i];
  }
  /* Set up 65c12 specific opcodes. */
  defs_65c12_set_opcode(0x02, k_nop, k_imm); /* Undocumented. */
  defs_65c12_set_opcode(0x04, k_tsb, k_zpg);
  defs_65c12_set_opcode(0x0C, k_tsb, k_abs);
  defs_65c12_set_opcode(0x12, k_ora, k_id);
  defs_65c12_set_opcode(0x14, k_trb, k_zpg);
  defs_65c12_set_opcode(0x1A, k_inc, k_acc);
  defs_65c12_set_opcode(0x1C, k_trb, k_abs);
  defs_65c12_set_opcode(0x22, k_nop, k_imm); /* Undocumented. */
  defs_65c12_set_opcode(0x32, k_and, k_id);
  defs_65c12_set_opcode(0x34, k_bit, k_zpx);
  defs_65c12_set_opcode(0x3A, k_dec, k_acc);
  defs_65c12_set_opcode(0x3C, k_bit, k_abx);
  defs_65c12_set_opcode(0x42, k_nop, k_imm); /* Undocumented. */
  defs_65c12_set_opcode(0x52, k_eor, k_id);
  defs_65c12_set_opcode(0x5A, k_phy, k_nil);
  defs_65c12_set_opcode(0x62, k_nop, k_imm); /* Undocumented. */
  defs_65c12_set_opcode(0x64, k_stz, k_zpg);
  defs_65c12_set_opcode(0x72, k_adc, k_id);
  defs_65c12_set_opcode(0x74, k_stz, k_zpx);
  defs_65c12_set_opcode(0x7A, k_ply, k_nil);
  defs_65c12_set_opcode(0x7C, k_jmp, k_iax);
  defs_65c12_set_opcode(0x80, k_bra, k_rel);
  defs_65c12_set_opcode(0x82, k_nop, k_imm); /* Undocumented. */
  defs_65c12_set_opcode(0x89, k_bit, k_imm);
  defs_65c12_set_opcode(0x92, k_sta, k_id);
  defs_65c12_set_opcode(0x9C, k_stz, k_abs);
  defs_65c12_set_opcode(0x9E, k_stz, k_abx);
  defs_65c12_set_opcode(0xB2, k_lda, k_id);
  defs_65c12_set_opcode(0xC2, k_nop, k_imm); /* Undocumented. */
  defs_65c12_set_opcode(0xD2, k_cmp, k_id);
  defs_65c12_set_opcode(0xDA, k_phx, k_nil);
  defs_65c12_set_opcode(0xE2, k_nop, k_imm); /* Undocumented. */
  defs_65c12_set_opcode(0xF2, k_sbc, k_id);
  defs_65c12_set_opcode(0xFA, k_plx, k_nil);
  /* 1-byte NOPs. */
  for (i = 0; i < 16; ++i) {
    defs_65c12_set_opcode(((i * 0x10) + 0x03), k_nop, k_nil1);
    defs_65c12_set_opcode(((i * 0x10) + 0x07), k_nop, k_nil1);
    defs_65c12_set_opcode(((i * 0x10) + 0x0B), k_nop, k_nil1);
    defs_65c12_set_opcode(((i * 0x10) + 0x0F), k_nop, k_nil1);
  }

  defs_6502_poplate_opmem_table(&s_opmem_65c12[0],
                                &s_optypes_65c12[0],
                                &s_opmodes_65c12[0]);
}

void
defs_6502_init() {
  if (s_inited) {
    return;
  }
  s_inited = 1;

  defs_6502_setup_6502();
  defs_6502_setup_65c12();
}
