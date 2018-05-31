#include "opdefs.h"

const char* g_p_opnames[58] = {
  "!!!", "???", "BRK", "ORA", "ASL", "PHP", "BPL", "CLC",
  "JSR", "AND", "BIT", "PLP", "ROL", "BMI", "SEC", "RTI",
  "EOR", "LSR", "PHA", "JMP", "BVC", "CLI", "RTS", "ADC",
  "PLA", "ROR", "BVS", "SEI", "STY", "STA", "STX", "DEY",
  "TXA", "BCC", "TYA", "TXS", "LDY", "LDA", "LDX", "TAY",
  "TAX", "BCS", "CLV", "TSX", "CPY", "CMP", "CPX", "DEC",
  "INY", "DEX", "BNE", "CLD", "SBC", "INX", "NOP", "INC",
  "BEQ", "SED",
};

unsigned char g_optypes[256] =
{
  // 0x00
  k_brk, k_ora, k_kil, k_unk, k_unk, k_ora, k_asl, k_unk,
  k_php, k_ora, k_asl, k_unk, k_unk, k_ora, k_asl, k_unk,
  // 0x10
  k_bpl, k_ora, k_kil, k_unk, k_unk, k_ora, k_asl, k_unk,
  k_clc, k_ora, k_unk, k_unk, k_unk, k_ora, k_asl, k_unk,
  // 0x20
  k_jsr, k_and, k_kil, k_unk, k_bit, k_and, k_rol, k_unk,
  k_plp, k_and, k_rol, k_unk, k_bit, k_and, k_rol, k_unk,
  // 0x30
  k_bmi, k_and, k_kil, k_unk, k_unk, k_and, k_rol, k_unk,
  k_sec, k_and, k_unk, k_unk, k_unk, k_and, k_rol, k_unk,
  // 0x40
  k_rti, k_eor, k_kil, k_unk, k_unk, k_eor, k_lsr, k_unk,
  k_pha, k_eor, k_lsr, k_unk, k_jmp, k_eor, k_lsr, k_unk,
  // 0x50
  k_bvc, k_eor, k_kil, k_unk, k_unk, k_eor, k_lsr, k_unk,
  k_cli, k_eor, k_unk, k_unk, k_unk, k_eor, k_lsr, k_unk,
  // 0x60
  k_rts, k_adc, k_kil, k_unk, k_unk, k_adc, k_ror, k_unk,
  k_pla, k_adc, k_ror, k_unk, k_jmp, k_adc, k_ror, k_unk,
  // 0x70
  k_bvs, k_adc, k_kil, k_unk, k_unk, k_adc, k_ror, k_unk,
  k_sei, k_adc, k_unk, k_unk, k_unk, k_adc, k_ror, k_unk,
  // 0x80
  k_unk, k_sta, k_unk, k_unk, k_sty, k_sta, k_stx, k_unk,
  k_dey, k_unk, k_txa, k_unk, k_sty, k_sta, k_stx, k_unk,
  // 0x90
  k_bcc, k_sta, k_kil, k_unk, k_sty, k_sta, k_stx, k_unk,
  k_tya, k_sta, k_txs, k_unk, k_unk, k_sta, k_unk, k_unk,
  // 0xa0
  k_ldy, k_lda, k_ldx, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  k_tay, k_lda, k_tax, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  // 0xb0
  k_bcs, k_lda, k_kil, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  k_clv, k_lda, k_tsx, k_unk, k_ldy, k_lda, k_ldx, k_unk,
  // 0xc0
  k_cpy, k_cmp, k_unk, k_unk, k_cpy, k_cmp, k_dec, k_unk,
  k_iny, k_cmp, k_dex, k_unk, k_cpy, k_cmp, k_dec, k_unk,
  // 0xd0
  k_bne, k_cmp, k_kil, k_unk, k_unk, k_cmp, k_dec, k_unk,
  k_cld, k_cmp, k_unk, k_unk, k_unk, k_cmp, k_dec, k_unk,
  // 0xe0
  k_cpx, k_sbc, k_unk, k_unk, k_cpx, k_sbc, k_inc, k_unk,
  k_inx, k_sbc, k_nop, k_unk, k_cpx, k_sbc, k_inc, k_unk,
  // 0xf0
  k_beq, k_sbc, k_kil, k_unk, k_unk, k_sbc, k_inc, k_unk,
  k_sed, k_sbc, k_unk, k_unk, k_unk, k_sbc, k_inc, k_unk,
};

unsigned char g_opmodes[256] =
{
  // 0x00
  k_nil, k_idx, 0    , 0    , 0    , k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , 0    , k_abs, k_abs, 0    ,
  // 0x10
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  // 0x20
  k_abs, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0x30
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  // 0x40
  k_nil, k_idx, 0    , 0    , 0    , k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0x50
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  // 0x60
  k_nil, k_idx, 0    , 0    , 0    , k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_ind, k_abs, k_abs, 0    ,
  // 0x70
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  // 0x80
  0    , k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, 0    , k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0x90
  k_imm, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpy, 0    ,
  k_nil, k_aby, k_nil, 0    , 0    , k_abx, 0    , 0    ,
  // 0xa0
  k_imm, k_idx, k_imm, 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0xb0
  k_imm, k_idy, 0    , 0    , k_zpx, k_zpx, k_zpy, 0    ,
  k_nil, k_aby, k_nil, 0    , k_abx, k_abx, k_aby, 0    ,
  // 0xc0
  k_imm, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0xd0
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
  // 0xe0
  k_imm, k_idx, 0    , 0    , k_zpg, k_zpg, k_zpg, 0    ,
  k_nil, k_imm, k_nil, 0    , k_abs, k_abs, k_abs, 0    ,
  // 0xf0
  k_imm, k_idy, 0    , 0    , 0    , k_zpx, k_zpx, 0    ,
  k_nil, k_aby, 0    , 0    , 0    , k_abx, k_abx, 0    ,
};

