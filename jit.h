#ifndef BEEBJIT_JIT_H
#define BEEBJIT_JIT_H

#include <stddef.h>
#include <stdint.h>

struct bbc_options;
struct bbc_timing;
struct debug_struct;
struct memory_access;
struct state_6502;

struct jit_struct;

enum {
  k_jit_flag_merge_ops = 1,
  k_jit_flag_self_modifying_abs = 2,
  k_jit_flag_dynamic_operand = 4,
  k_jit_flag_self_modifying_all = 8,
  k_jit_flag_batch_ops = 16,
  k_jit_flag_elim_nz_flag_tests = 32,
};

struct jit_struct* jit_create(struct state_6502* p_state_6502,
                              struct memory_access* p_memory_access,
                              struct bbc_timing* p_timing,
                              struct bbc_options* p_options);
void jit_destroy(struct jit_struct* p_jit);

void jit_set_flag(struct jit_struct* p_jit, unsigned int flag);
void jit_clear_flag(struct jit_struct* p_jit, unsigned int flag);
void jit_set_max_compile_ops(struct jit_struct* p_jit, size_t max_num_ops);

uint16_t jit_block_from_6502(struct jit_struct* p_jit, uint16_t addr_6502);
void jit_memory_written(struct jit_struct* p_jit, uint16_t addr_6502);

void jit_init_addr(struct jit_struct* p_jit, uint16_t addr_6502);
int jit_has_code(struct jit_struct* p_jit, uint16_t addr_6502);
int jit_is_block_start(struct jit_struct* p_jit, uint16_t addr_6502);
int jit_has_invalidated_code(struct jit_struct* p_jit, uint16_t addr_6502);
int jit_jump_target_is_invalidated(struct jit_struct* p_jit,
                                   uint16_t addr_6502);
int jit_is_compilation_pending(struct jit_struct* p_jit, uint16_t addr_6502);
int jit_has_self_modify_optimize(struct jit_struct* p_jit, uint16_t addr_6502);
unsigned char* jit_get_code_ptr(struct jit_struct* p_jit, uint16_t addr_6502);
unsigned char* jit_get_jit_base_addr(struct jit_struct* p_jit,
                                     uint16_t addr_6502);

void jit_set_counter(struct jit_struct* p_jit, size_t counter);

void jit_enter(struct jit_struct* p_jit);

void jit_async_timer_tick(struct jit_struct* p_jit);

#endif /* BEEJIT_JIT_H */
