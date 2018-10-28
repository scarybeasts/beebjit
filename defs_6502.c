#include "defs_6502.h"

const char* g_p_opnames[k_6502_op_num_types] =
{
  "!!!", "???", "BRK", "ORA", "ASL", "PHP", "BPL", "CLC",
  "JSR", "AND", "BIT", "PLP", "ROL", "BMI", "SEC", "RTI",
  "EOR", "LSR", "PHA", "JMP", "BVC", "CLI", "RTS", "ADC",
  "PLA", "ROR", "BVS", "SEI", "STY", "STA", "STX", "DEY",
  "TXA", "BCC", "TYA", "TXS", "LDY", "LDA", "LDX", "TAY",
  "TAX", "BCS", "CLV", "TSX", "CPY", "CMP", "CPX", "DEC",
  "INY", "DEX", "BNE", "CLD", "SBC", "INX", "NOP", "INC",
  "BEQ", "SED", "SAX", "ALR", "SLO",
};

unsigned char g_opmem[k_6502_op_num_types] = {
  k_nomem, k_nomem, k_nomem, k_read , k_rw   , k_nomem, k_nomem, k_nomem,
  k_nomem, k_read , k_read , k_nomem, k_rw   , k_nomem, k_nomem, k_nomem,
  k_read , k_rw   , k_nomem, k_nomem, k_nomem, k_nomem, k_nomem, k_read ,
  k_nomem, k_rw   , k_nomem, k_nomem, k_write, k_write, k_write, k_nomem,
  k_nomem, k_nomem, k_nomem, k_nomem, k_read , k_read , k_read , k_nomem,
  k_nomem, k_nomem, k_nomem, k_nomem, k_read , k_read , k_read , k_rw   ,
  k_nomem, k_nomem, k_nomem, k_nomem, k_read , k_nomem, k_read , k_rw   ,
  k_nomem, k_nomem, k_write, k_nomem, k_rw   ,
};

unsigned char g_opbranch[k_6502_op_num_types] = {
  k_bra_y, k_bra_n, k_bra_y, k_bra_n, k_bra_n, k_bra_n, k_bra_m, k_bra_n,
  k_bra_y, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_m, k_bra_n, k_bra_y,
  k_bra_n, k_bra_n, k_bra_n, k_bra_y, k_bra_m, k_bra_n, k_bra_y, k_bra_n,
  k_bra_n, k_bra_n, k_bra_m, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
  k_bra_n, k_bra_m, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
  k_bra_n, k_bra_m, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
  k_bra_n, k_bra_n, k_bra_m, k_bra_n, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
  k_bra_m, k_bra_n, k_bra_n, k_bra_n, k_bra_n,
};

unsigned char g_optype_uses_carry[k_6502_op_num_types] = {
  0, 0, 0, 0, 0, 1, 0, 0, /* PHP */
  0, 0, 0, 0, 1, 0, 0, 0, /* ROL */
  0, 0, 0, 0, 0, 0, 0, 1, /* ADC */
  0, 1, 0, 0, 0, 0, 0, 0, /* ROR */
  0, 1, 0, 0, 0, 0, 0, 0, /* BCC */
  0, 1, 0, 0, 0, 0, 0, 0, /* BCS */
  0, 0, 0, 0, 1, 0, 0, 0, /* SBC */
  0, 0, 0, 0, 0,
};

unsigned char g_optype_changes_carry[k_6502_op_num_types] = {
  0, 0, 0, 0, 1, 0, 0, 1, /* ASL, CLC */
  0, 0, 0, 1, 1, 0, 1, 1, /* PLP, ROL, SEC, RTI */
  0, 1, 0, 0, 0, 0, 0, 1, /* LSR, ADC */
  0, 1, 0, 0, 0, 0, 0, 0, /* ROR */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 1, 1, 1, 0, /* CPY, CMP, CPX */
  0, 0, 0, 0, 1, 0, 0, 0, /* SBC */
  0, 0, 0, 1, 1,          /* ALR, SLO */
};

unsigned char g_optype_changes_overflow[k_6502_op_num_types] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 1, 0, 0, 0, 1, /* BIT, PLP, RTI */
  0, 0, 0, 0, 0, 0, 0, 1, /* ADC */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 0, 0, 0, 0, 0, /* CLV */
  0, 0, 0, 0, 1, 0, 0, 0, /* SBC */
  0, 0, 0, 0, 0,
};

unsigned char g_optype_sets_register[k_6502_op_num_types] =
{
  0  , 0  , 0  , k_a, 0  , 0  , 0  , 0  ,
  0  , k_a, 0  , 0  , 0  , 0  , 0  , 0  ,
  k_a, 0  , 0  , 0  , 0  , 0  , 0  , k_a,
  k_a, 0  , 0  , 0  , 0  , 0  , 0  , k_y,
  k_a, 0  , k_a, 0  , k_y, k_a, k_x, k_y,
  k_x, 0  , 0  , k_x, 0  , 0  , 0  , 0  ,
  k_y, k_x, 0  , 0  , k_a, k_x, 0  , 0  ,
  0  , 0  , 0  , k_a, k_a,
};

unsigned char g_optypes[k_6502_op_num_opcodes] =
{
  /* 0x00 */
  k_brk, k_ora, k_kil, k_unk, k_nop, k_ora, k_asl, k_slo,
  k_php, k_ora, k_asl, k_unk, k_unk, k_ora, k_asl, k_unk,
  /* 0x10 */
  k_bpl, k_ora, k_kil, k_unk, k_unk, k_ora, k_asl, k_unk,
  k_clc, k_ora, k_unk, k_unk, k_unk, k_ora, k_asl, k_unk,
  /* 0x20 */
  k_jsr, k_and, k_kil, k_unk, k_bit, k_and, k_rol, k_unk,
  k_plp, k_and, k_rol, k_unk, k_bit, k_and, k_rol, k_unk,
  /* 0x30 */
  k_bmi, k_and, k_kil, k_unk, k_unk, k_and, k_rol, k_unk,
  k_sec, k_and, k_unk, k_unk, k_unk, k_and, k_rol, k_unk,
  /* 0x40 */
  k_rti, k_eor, k_kil, k_unk, k_unk, k_eor, k_lsr, k_unk,
  k_pha, k_eor, k_lsr, k_alr, k_jmp, k_eor, k_lsr, k_unk,
  /* 0x50 */
  k_bvc, k_eor, k_kil, k_unk, k_unk, k_eor, k_lsr, k_unk,
  k_cli, k_eor, k_unk, k_unk, k_unk, k_eor, k_lsr, k_unk,
  /* 0x60 */
  k_rts, k_adc, k_kil, k_unk, k_unk, k_adc, k_ror, k_unk,
  k_pla, k_adc, k_ror, k_unk, k_jmp, k_adc, k_ror, k_unk,
  /* 0x70 */
  k_bvs, k_adc, k_kil, k_unk, k_unk, k_adc, k_ror, k_unk,
  k_sei, k_adc, k_unk, k_unk, k_unk, k_adc, k_ror, k_unk,
  /* 0x80 */
  k_unk, k_sta, k_unk, k_unk, k_sty, k_sta, k_stx, k_sax,
  k_dey, k_unk, k_txa, k_unk, k_sty, k_sta, k_stx, k_unk,
  /* 0x90 */
  k_bcc, k_sta, k_kil, k_unk, k_sty, k_sta, k_stx, k_unk,
  k_tya, k_sta, k_txs, k_unk, k_unk, k_sta, k_unk, k_unk,
  /* 0xa0 */
  k_ldy, k_lda, k_ldx, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  k_tay, k_lda, k_tax, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  /* 0xb0 */
  k_bcs, k_lda, k_kil, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  k_clv, k_lda, k_tsx, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  /* 0xc0 */
  k_cpy, k_cmp, k_unk, k_unk, k_cpy, k_cmp, k_dec, k_unk,
  k_iny, k_cmp, k_dex, k_unk, k_cpy, k_cmp, k_dec, k_unk,
  /* 0xd0 */
  k_bne, k_cmp, k_kil, k_unk, k_unk, k_cmp, k_dec, k_unk,
  k_cld, k_cmp, k_unk, k_unk, k_nop, k_cmp, k_dec, k_unk,
  /* 0xe0 */
  k_cpx, k_sbc, k_unk, k_unk, k_cpx, k_sbc, k_inc, k_unk,
  k_inx, k_sbc, k_nop, k_unk, k_cpx, k_sbc, k_inc, k_unk,
  /* 0xf0 */
  k_beq, k_sbc, k_kil, k_unk, k_nop, k_sbc, k_inc, k_unk,
  k_sed, k_sbc, k_unk, k_unk, k_unk, k_sbc, k_inc, k_unk,
};

unsigned char g_opmodes[k_6502_op_num_opcodes] =
{
  /* 0x00 */
  k_nil, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, k_imm, k_acc, 0    , 0    , k_abs, k_abs, 0    ,
  /* 0x10 */
  k_rel, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  /* 0x20 */
  k_abs, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_acc, 0    , k_abs, k_abs, k_abs, 0    ,
  /* 0x30 */
  k_rel, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  /* 0x40 */
  k_nil, k_idx, 0    , 0    , 0    , k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_acc, k_imm, k_abs, k_abs, k_abs, 0    ,
  /* 0x50 */
  k_rel, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  /* 0x60 */
  k_nil, k_idx, 0    , 0    , 0    , k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_acc, 0    , k_ind, k_abs, k_abs, 0    ,
  /* 0x70 */
  k_rel, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  /* 0x80 */
  0    , k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, k_zpg,
  k_nil, 0    , k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  /* 0x90 */
  k_rel, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpy, 0    ,
  k_nil, k_aby, k_nil, 0    , 0    , k_abx, 0    , 0    ,
  /* 0xa0 */
  k_imm, k_idx, k_imm, 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  /* 0xb0 */
  k_rel, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpy, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, k_aby, 0    ,
  /* 0xc0 */
  k_imm, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  /* 0xd0 */
  k_rel, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , k_abx, k_abx, k_abx, 0    ,
  /* 0xe0 */
  k_imm, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  /* 0xf0 */
  k_rel, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
};

unsigned char g_opcycles[k_6502_op_num_opcodes] =
{
  /* 0x00 */
  7, 6, 0, 0, 3, 3, 5, 5,
  3, 2, 2, 0, 0, 4, 6, 0,
  /* 0x10 */
  2, 5, 0, 0, 0, 4, 6, 0,
  2, 4, 0, 0, 0, 4, 7, 0,
  /* 0x20 */
  6, 6, 0, 0, 3, 3, 5, 0,
  4, 2, 2, 0, 4, 4, 6, 0,
  /* 0x30 */
  2, 5, 0, 0, 0, 4, 6, 0,
  2, 4, 0, 0, 0, 4, 7, 0,
  /* 0x40 */
  6, 6, 0, 0, 0, 3, 5, 0,
  3, 2, 2, 2, 3, 4, 6, 0,
  /* 0x50 */
  2, 5, 0, 0, 0, 4, 6, 0,
  2, 4, 0, 0, 0, 4, 7, 0,
  /* 0x60 */
  6, 6, 0, 0, 0, 3, 5, 0,
  4, 2, 2, 0, 5, 4, 6, 0,
  /* 0x70 */
  2, 5, 0, 0, 0, 4, 6, 0,
  2, 4, 0, 0, 0, 4, 7, 0,
  /* 0x80 */
  0, 6, 0, 0, 3, 3, 3, 3,
  2, 0, 2, 0, 4, 4, 4, 0,
  /* 0x90 */
  2, 6, 0, 0, 4, 4, 4, 0,
  2, 5, 2, 0, 0, 5, 0, 0,
  /* 0xa0 */
  2, 6, 2, 0, 3, 3, 3, 0,
  2, 2, 2, 0, 4, 4, 4, 0,
  /* 0xb0 */
  2, 5, 0, 0, 4, 4, 4, 0,
  2, 4, 2, 0, 4, 4, 4, 0,
  /* 0xc0 */
  2, 6, 0, 0, 3, 3, 5, 0,
  2, 2, 2, 0, 4, 4, 6, 0,
  /* 0xd0 */
  2, 5, 0, 0, 0, 4, 6, 0,
  2, 4, 0, 0, 4, 4, 7, 0,
  /* 0xe0 */
  2, 6, 0, 0, 3, 3, 5, 0,
  2, 2, 2, 0, 4, 4, 6, 0,
  /* 0xf0 */
  2, 5, 0, 0, 4, 4, 6, 0,
  2, 4, 0, 0, 0, 4, 7, 0,
};


unsigned char g_opmodelens[k_6502_op_num_modes] =
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
