#ifndef BEEBJIT_DEFS_6502_H
#define BEEBJIT_DEFS_6502_H

#include <stdint.h>

enum {
  k_6502_addr_space_size = 0x10000,
  k_6502_vector_nmi = 0xFFFA,
  k_6502_vector_reset = 0xFFFC,
  k_6502_vector_irq = 0xFFFE,
  k_6502_stack_addr = 0x100,
};

enum {
  k_6502_op_num_types = 88,
  k_6502_op_num_opcodes = 256,
  k_6502_op_num_modes = 17,
};

extern const char* g_p_opnames[k_6502_op_num_types];
extern uint8_t g_opmodelens[k_6502_op_num_modes];
extern uint8_t g_opmem[k_6502_op_num_types];
extern uint8_t g_opbranch[k_6502_op_num_types];
extern uint8_t g_optype_uses_carry[k_6502_op_num_types];
extern uint8_t g_optype_changes_nz_flags[k_6502_op_num_types];
extern uint8_t g_optype_changes_carry[k_6502_op_num_types];
extern uint8_t g_optype_changes_overflow[k_6502_op_num_types];
extern uint8_t g_optype_sets_register[k_6502_op_num_types];

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
  k_last_6502_documented = 57,
  k_sax = 58, /* Undocumented. */
  k_alr = 59, /* Undocumented. */
  k_slo = 60, /* Undocumented. */
  k_shy = 61, /* Undocumented. */
  k_anc = 62, /* Undocumented. */
  k_lax = 63, /* Undocumented. */
  k_dcp = 64, /* Undocumented. */
  k_sre = 65, /* Undocumented. */
  k_rla = 66, /* Undocumented. */
  k_ahx = 67, /* Undocumented. */
  k_xaa = 68, /* Undocumented. */
  k_rra = 69, /* Undocumented. */
  k_axs = 70, /* Undocumented. */
  k_isc = 71, /* Undocumented. */
  k_arr = 72, /* Undocumented. */
  k_tas = 73, /* Undocumented. */
  k_las = 74, /* Undocumented. */
  k_shx = 75, /* Undocumented. */
  k_tsb = 80, /* 65c12. */
  k_trb = 81, /* 65c12. */
  k_stz = 82, /* 65c12. */
  k_bra = 83, /* 65c12. */
  k_phx = 84, /* 65c12. */
  k_phy = 85, /* 65c12. */
  k_plx = 86, /* 65c12. */
  k_ply = 87, /* 65c12. */
};

enum {
  k_nil = 1,
  k_acc = 2,
  k_imm = 3,
  k_zpg = 4,
  k_abs = 5,
  k_zpx = 6,
  k_zpy = 7,
  k_abx = 8,
  k_aby = 9,
  k_idx = 10,
  k_idy = 11,
  k_ind = 12,
  k_rel = 13,
  /* Three new modes in the 65c12. */
  k_iax = 14,
  k_id = 15,
  k_nil1 = 16,
  /* Additional address modes supported for efficient self-modifying code. */
  k_imm_dyn = 17,
  k_zpg_dyn = 18,
  k_abs_dyn = 19,
  k_abx_dyn = 20,
  k_aby_dyn = 21,
  k_idy_dyn = 22,
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

enum {
  k_a = 1,
  k_x = 2,
  k_y = 3,
};

enum {
  k_flag_carry = 0,
  k_flag_zero = 1,
  k_flag_interrupt = 2,
  k_flag_decimal = 3,
  k_flag_brk = 4,
  k_flag_always_set = 5,
  k_flag_overflow = 6,
  k_flag_negative = 7,
};

void defs_6502_init();

uint8_t* defs_6502_get_6502_optype_map();
uint8_t* defs_6502_get_6502_opmode_map();
uint8_t* defs_6502_get_6502_opcycles_map();
uint8_t* defs_6502_get_65c12_optype_map();
uint8_t* defs_6502_get_65c12_opmode_map();
uint8_t* defs_6502_get_65c12_opcycles_map();

#endif /* BEEBJIT_DEFS_6502_H */
