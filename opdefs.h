#ifndef BEEBJIT_OPDEFS_H
#define BEEBJIT_OPDEFS_H

const char* g_p_opnames[58];
unsigned char g_optypes[256];
unsigned char g_opmodes[256];
unsigned char g_opmodelens[13];
unsigned char g_opmem[58];
unsigned char g_opbranch[58];

enum {
  k_kil = 0,
  k_unk = 1,
  k_brk = 2,
  k_ora = 3,
  k_asl = 4,
  k_php = 5,
  k_bpl = 6,
  k_clc = 7,
  k_jsr = 8,
  k_and = 9,
  k_bit = 10,
  k_plp = 11,
  k_rol = 12,
  k_bmi = 13,
  k_sec = 14,
  k_rti = 15,
  k_eor = 16,
  k_lsr = 17,
  k_pha = 18,
  k_jmp = 19,
  k_bvc = 20,
  k_cli = 21,
  k_rts = 22,
  k_adc = 23,
  k_pla = 24,
  k_ror = 25,
  k_bvs = 26,
  k_sei = 27,
  k_sty = 28,
  k_sta = 29,
  k_stx = 30,
  k_dey = 31,
  k_txa = 32,
  k_bcc = 33,
  k_tya = 34,
  k_txs = 35,
  k_ldy = 36,
  k_lda = 37,
  k_ldx = 38,
  k_tay = 39,
  k_tax = 40,
  k_bcs = 41,
  k_clv = 42,
  k_tsx = 43,
  k_cpy = 44,
  k_cmp = 45,
  k_cpx = 46,
  k_dec = 47,
  k_iny = 48,
  k_dex = 49,
  k_bne = 50,
  k_cld = 51,
  k_sbc = 52,
  k_inx = 53,
  k_nop = 54,
  k_inc = 55,
  k_beq = 56,
  k_sed = 57,
};

enum {
  k_nil = 1,
  k_imm = 2,
  k_zpg = 3,
  k_abs = 4,
  k_zpx = 5,
  k_zpy = 6,
  k_abx = 7,
  k_aby = 8,
  k_idx = 9,
  k_idy = 10,
  k_ind = 11,
  k_rel = 12,
};

enum {
  k_nomem = 0,
  k_read = 1,
  k_write = 2,
  k_rw = 3,
};

enum {
  k_bra_n = 0,
  k_bra_y = 1,
  k_bra_m = 2,
};

#endif /* BEEBJIT_OPDEFS_H */
