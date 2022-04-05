#ifndef BEEBJIT_JIT_OPTIMIZER_H
#define BEEBJIT_JIT_OPTIMIZER_H

#include <stdint.h>

struct jit_opcode_details;

struct jit_opcode_details*
jit_optimizer_optimize_pre_rewrite(struct jit_opcode_details* p_opcodes);

void jit_optimizer_optimize_post_rewrite(struct jit_opcode_details* p_opcodes);

#endif /* BEEJIT_JIT_OPTIMIZER_H */
