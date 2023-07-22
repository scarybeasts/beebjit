#ifndef BEEBJIT_JIT_OPTIMIZER_H
#define BEEBJIT_JIT_OPTIMIZER_H

#include <stdint.h>

struct jit_opcode_details;
struct jit_metadata;

void jit_optimizer_optimize_pre_rewrite(struct jit_opcode_details* p_opcodes,
                                        struct jit_metadata* p_metadata,
                                        int do_collapse_loops);

void jit_optimizer_optimize_post_rewrite(struct jit_opcode_details* p_opcodes);

#endif /* BEEJIT_JIT_OPTIMIZER_H */
