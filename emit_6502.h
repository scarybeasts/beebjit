#ifndef BEEBJIT_EMIT_6502_H
#define BEEBJIT_EMIT_6502_H

#include <stdint.h>

struct util_buffer;

enum {
  k_emit_crash_len = 3,
};

void emit_ADC(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_AND(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_ASL(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_BCC(struct util_buffer* p_buf, char offset);
void emit_BCS(struct util_buffer* p_buf, char offset);
void emit_BEQ(struct util_buffer* p_buf, char offset);
void emit_BIT(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_BMI(struct util_buffer* p_buf, char offset);
void emit_BNE(struct util_buffer* p_buf, char offset);
void emit_BPL(struct util_buffer* p_buf, char offset);
void emit_BRK(struct util_buffer* p_buf);
void emit_BVC(struct util_buffer* p_buf, char offset);
void emit_BVS(struct util_buffer* p_buf, char offset);
void emit_CLC(struct util_buffer* p_buf);
void emit_CLD(struct util_buffer* p_buf);
void emit_CLI(struct util_buffer* p_buf);
void emit_CLV(struct util_buffer* p_buf);
void emit_CMP(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_CPX(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_CPY(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_DEC(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_DEX(struct util_buffer* p_buf);
void emit_DEY(struct util_buffer* p_buf);
void emit_EOR(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_INC(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_INX(struct util_buffer* p_buf);
void emit_INY(struct util_buffer* p_buf);
void emit_JMP(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_JSR(struct util_buffer* p_buf, uint16_t addr);
void emit_LDA(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_LDX(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_LDY(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_LSR(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_NOP(struct util_buffer* p_buf);
void emit_NOP1(struct util_buffer* p_buf);
void emit_ORA(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_PHA(struct util_buffer* p_buf);
void emit_PHP(struct util_buffer* p_buf);
void emit_PHX(struct util_buffer* p_buf);
void emit_PLA(struct util_buffer* p_buf);
void emit_PLP(struct util_buffer* p_buf);
void emit_PLX(struct util_buffer* p_buf);
void emit_ROL(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_ROR(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_RTI(struct util_buffer* p_buf);
void emit_RTS(struct util_buffer* p_buf);
void emit_SBC(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_SEC(struct util_buffer* p_buf);
void emit_SED(struct util_buffer* p_buf);
void emit_SEI(struct util_buffer* p_buf);
void emit_STA(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_STX(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_STY(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_STZ(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_TAX(struct util_buffer* p_buf);
void emit_TAY(struct util_buffer* p_buf);
void emit_TSX(struct util_buffer* p_buf);
void emit_TXA(struct util_buffer* p_buf);
void emit_TXS(struct util_buffer* p_buf);
void emit_TYA(struct util_buffer* p_buf);

void emit_CRASH(struct util_buffer* p_buf);
void emit_CYCLES(struct util_buffer* p_buf);
void emit_CYCLES_RESET(struct util_buffer* p_buf);
void emit_EXIT(struct util_buffer* p_buf);

#endif /* BEEBJIT_EMIT_6502_H */
