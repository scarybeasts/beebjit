#ifndef BEEBJIT_JIT_COMPILER_H
#define BEEBJIT_JIT_COMPILER_H

#include <stdint.h>

struct asm_jit_struct;
struct bbc_options;
struct jit_compiler;
struct jit_opcode_details;
struct memory_access;
struct state_6502;
struct timing_struct;
struct util_buffer;

struct jit_compiler* jit_compiler_create(
    struct asm_jit_struct* p_asm,
    struct timing_struct* p_timing,
    struct memory_access* p_memory_access,
    void* (*get_block_host_address)(void* p, uint16_t addr),
    void* p_host_address_object,
    uint32_t* p_jit_ptrs,
    void* p_jit_ptr_no_code,
    void* p_jit_ptr_dynamic_operand,
    int32_t* p_code_blocks,
    struct bbc_options* p_options,
    int debug,
    uint8_t* p_opcode_types,
    uint8_t* p_opcode_modes,
    uint8_t* p_opcode_mem,
    uint8_t* p_opcode_cycles);
void jit_compiler_destroy(struct jit_compiler* p_compiler);

uint32_t jit_compiler_compile_block(struct jit_compiler* p_compiler,
                                    int is_invalidation,
                                    uint16_t addr_6502);

int64_t jit_compiler_fixup_state(struct jit_compiler* p_compiler,
                                 struct state_6502* p_state_6502,
                                 int64_t countdown,
                                 uint64_t host_rflags);

void jit_compiler_memory_range_invalidate(struct jit_compiler* p_compiler,
                                          uint16_t addr,
                                          uint32_t len);

int jit_compiler_is_block_continuation(struct jit_compiler* p_compiler,
                                       uint16_t addr_6502);

int jit_compiler_is_compiling_for_code_in_zero_page(
    struct jit_compiler* p_compiler);
void jit_compiler_set_compiling_for_code_in_zero_page(
    struct jit_compiler* p_compiler, int value);

void jit_compiler_testing_set_optimizing(struct jit_compiler* p_compiler,
                                         int is_optimizing);
void jit_compiler_testing_set_dynamic_operand(struct jit_compiler* p_compiler,
                                              int is_dynamic_operand);
void jit_compiler_testing_set_dynamic_opcode(struct jit_compiler* p_compiler,
                                             int is_dynamic_opcode);
void jit_compiler_testing_set_sub_instruction(struct jit_compiler* p_compiler,
                                              int is_sub_instruction);
void jit_compiler_testing_set_max_ops(struct jit_compiler* p_compiler,
                                      uint32_t num_ops);
void jit_compiler_testing_set_dynamic_trigger(
    struct jit_compiler* p_compiler, uint32_t count);
void jit_compiler_testing_set_accurate_cycles(struct jit_compiler* p_compiler,
                                              int is_accurate);
int32_t jit_compiler_testing_get_cycles_fixup(struct jit_compiler* p_compiler,
                                              uint16_t addr);
int32_t jit_compiler_testing_get_a_fixup(struct jit_compiler* p_compiler,
                                         uint16_t addr);
int32_t jit_compiler_testing_get_x_fixup(struct jit_compiler* p_compiler,
                                         uint16_t addr);
int jit_has_invalidated_code(struct jit_compiler* p_compiler, uint16_t addr);

#endif /* BEEJIT_JIT_COMPILER_H */
