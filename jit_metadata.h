#ifndef BEEBJIT_JIT_METADATA_H
#define BEEBJIT_JIT_METADATA_H

struct jit_metadata;

#include <stdint.h>

struct jit_metadata* jit_metadata_create(void* p_jit_base,
                                         void* p_jit_ptr_no_code,
                                         void* p_jit_ptr_dynamic,
                                         uint32_t* p_jit_ptrs);
void jit_metadata_destroy(struct jit_metadata* p_metadata);

void* jit_metadata_get_host_block_address(struct jit_metadata* p_metadata,
                                          uint16_t addr_6502);
void* jit_metadata_get_host_jit_ptr(struct jit_metadata* p_metadata,
                                    uint16_t addr_6502);
int jit_metadata_is_pc_in_code_block(struct jit_metadata* p_metadata,
                                     uint16_t addr_6502);
int32_t jit_metadata_get_code_block(struct jit_metadata* p_metadata,
                                    uint16_t addr_6502);
int jit_metadata_is_jit_ptr_no_code(struct jit_metadata* p_metadata,
                                    void* p_jit_ptr);
int jit_metadata_is_jit_ptr_dynamic(struct jit_metadata* p_metadata,
                                    void* p_jit_ptr);
int jit_metadata_has_invalidated_code(struct jit_metadata* p_metadata,
                                      uint16_t addr_6502);
uint16_t jit_metadata_get_block_addr_from_host_pc(
    struct jit_metadata* p_metadata, void* p_host_pc);
uint16_t jit_metadata_get_6502_pc_from_host_pc(struct jit_metadata* p_metadata,                                                void* p_host_pc);

void jit_metadata_set_jit_ptr(struct jit_metadata* p_metadata,
                              uint16_t addr_6502,
                              uint32_t jit_ptr);
void jit_metadata_make_jit_ptr_no_code(struct jit_metadata* p_metadata,
                                       uint16_t addr_6502);
void jit_metadata_make_jit_ptr_dynamic(struct jit_metadata* p_metadata,
                                       uint16_t addr_6502);
void jit_metadata_set_code_block(struct jit_metadata* p_metadata,
                                 uint16_t addr_6502,
                                 int32_t code_block);
void jit_metadata_invalidate_jump_target(struct jit_metadata* p_metadata,
                                         uint16_t addr);
void jit_metadata_invalidate_code(struct jit_metadata* p_metadata,
                                  uint16_t addr);
void jit_metadata_clear_block(struct jit_metadata* p_metadata,
                              uint16_t block_addr_6502);

#endif /* BEEBJIT_JIT_METADATA_H */
