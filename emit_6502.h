#ifndef BEEBJIT_EMIT_6502_H
#define BEEBJIT_EMIT_6502_H

#include <stdint.h>

struct util_buffer;

void emit_ADC(struct util_buffer* p_buf, int mode, uint16_t addr);
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
void emit_CMP(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_CPX(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_INX(struct util_buffer* p_buf);
void emit_INY(struct util_buffer* p_buf);
void emit_JMP(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_JSR(struct util_buffer* p_buf, uint16_t addr);
void emit_LDA(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_LDX(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_PHP(struct util_buffer* p_buf);
void emit_PLP(struct util_buffer* p_buf);
void emit_ROR(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_RTS(struct util_buffer* p_buf);
void emit_SBC(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_SEC(struct util_buffer* p_buf);
void emit_STA(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_STX(struct util_buffer* p_buf, int mode, uint16_t addr);
void emit_TSX(struct util_buffer* p_buf);
void emit_TXS(struct util_buffer* p_buf);

void emit_CRASH(struct util_buffer* p_buf);

#endif /* BEEBJIT_EMIT_6502_H */
