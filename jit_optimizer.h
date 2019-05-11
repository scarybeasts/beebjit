#ifndef BEEBJIT_JIT_OPTIMIZER_H
#define BEEBJIT_JIT_OPTIMIZER_H

#include <stdint.h>

struct jit_opcode_details;
struct jit_compiler;

void
jit_optimizer_optimize(struct jit_compiler* p_compiler,
                       struct jit_opcode_details* p_opcodes,
                       uint32_t num_opcodes);

#endif /* BEEJIT_JIT_OPTIMIZER_H */
