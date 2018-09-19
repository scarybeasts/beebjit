#ifndef BEEBJIT_JIT_H
#define BEEBJIT_JIT_H

#include <stddef.h>
#include <stdint.h>

struct bbc_struct;
struct debug_struct;

struct jit_struct;

struct jit_struct* jit_create(void* p_debug_callback,
                              struct debug_struct* p_debug,
                              struct bbc_struct* p_bbc,
                              void* p_read_callback,
                              void* p_write_callback,
                              const char* p_opt_flags,
                              const char* p_log_flags);
void jit_destroy(struct jit_struct* p_jit);

void jit_get_registers(struct jit_struct* p_jit,
                       unsigned char* a,
                       unsigned char* x,
                       unsigned char* y,
                       unsigned char* s,
                       unsigned char* flags,
                       uint16_t* pc);
void jit_set_registers(struct jit_struct* p_jit,
                       unsigned char a,
                       unsigned char x,
                       unsigned char y,
                       unsigned char s,
                       unsigned char flags,
                       uint16_t pc);
uint16_t jit_block_from_6502(struct jit_struct* p_jit, uint16_t addr_6502);
void jit_check_pc(struct jit_struct* p_jit);
void jit_memory_written(struct jit_struct* p_jit, uint16_t addr_6502);

int jit_has_code(struct jit_struct* p_jit, uint16_t addr_6502);
int jit_is_block_start(struct jit_struct* p_jit, uint16_t addr_6502);
int jit_has_invalidated_code(struct jit_struct* p_jit, uint16_t addr_6502);
int jit_jump_target_is_invalidated(struct jit_struct* p_jit,
                                   uint16_t addr_6502);
int jit_is_force_invalidated(struct jit_struct* p_jit, uint16_t addr_6502);
int jit_has_self_modify_optimize(struct jit_struct* p_jit, uint16_t addr_6502);
unsigned char* jit_get_code_ptr(struct jit_struct* p_jit, uint16_t addr_6502);

void jit_set_interrupt(struct jit_struct* p_jit, int id, int set);
void jit_set_counter(struct jit_struct* p_jit, size_t counter);

void jit_enter(struct jit_struct* p_jit);

void jit_async_timer_tick(struct jit_struct* p_jit);

void (*jit_get_jit_callback_for_testing())(struct jit_struct*, unsigned char*);

#endif /* BEEJIT_JIT_H */
