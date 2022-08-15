#include "jit_metadata.h"

#include "defs_6502.h"
#include "util.h"

#include "asm/asm_jit.h"
#include "asm/asm_jit_defs.h"

#include <assert.h>

struct jit_metadata {
  void* p_jit_base;
  void* p_jit_ptr_no_code;
  void* p_jit_ptr_dynamic;
  uint32_t* p_jit_ptrs;
  int32_t code_blocks[k_6502_addr_space_size];
};

struct jit_metadata*
jit_metadata_create(void* p_jit_base,
                    void* p_jit_ptr_no_code,
                    void* p_jit_ptr_dynamic,
                    uint32_t* p_jit_ptrs) {
  uint32_t i;
  struct jit_metadata* p_metadata = util_mallocz(sizeof(struct jit_metadata));

  p_metadata->p_jit_base = p_jit_base;
  p_metadata->p_jit_ptr_no_code = p_jit_ptr_no_code;
  p_metadata->p_jit_ptr_dynamic = p_jit_ptr_dynamic;
  p_metadata->p_jit_ptrs = p_jit_ptrs;

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    p_metadata->p_jit_ptrs[i] =
        (uint32_t) (uintptr_t) p_metadata->p_jit_ptr_no_code;
    p_metadata->code_blocks[i] = -1;
  }

  return p_metadata;
}

void
jit_metadata_destroy(struct jit_metadata* p_metadata) {
  util_free(p_metadata);
}

void*
jit_metadata_get_host_block_address(struct jit_metadata* p_metadata,
                                    uint16_t addr_6502) {
  void* p_jit_ptr = p_metadata->p_jit_base;
  p_jit_ptr += (addr_6502 * K_JIT_BYTES_PER_BYTE);
  return p_jit_ptr;
}

void*
jit_metadata_get_host_jit_ptr(struct jit_metadata* p_metadata,
                              uint16_t addr_6502) {
  void* p_jit_ptr;
  uintptr_t jit_ptr = (uintptr_t) p_metadata->p_jit_ptrs[addr_6502];

  jit_ptr |= (uintptr_t) p_metadata->p_jit_base;
  p_jit_ptr = (void*) jit_ptr;

  return p_jit_ptr;
}

int
jit_metadata_is_pc_in_code_block(struct jit_metadata* p_metadata,
                                 uint16_t addr_6502) {
  int ret = (p_metadata->code_blocks[addr_6502] != -1);
  return ret;
}

int32_t
jit_metadata_get_code_block(struct jit_metadata* p_metadata,
                            uint16_t addr_6502) {
  return p_metadata->code_blocks[addr_6502];
}

int
jit_metadata_is_jit_ptr_no_code(struct jit_metadata* p_metadata,
                                void* p_jit_ptr) {
  return (p_jit_ptr == p_metadata->p_jit_ptr_no_code);
}

int
jit_metadata_is_jit_ptr_dynamic(struct jit_metadata* p_metadata,
                                void* p_jit_ptr) {
  return (p_jit_ptr == p_metadata->p_jit_ptr_dynamic);
}

int
jit_metadata_has_invalidated_code(struct jit_metadata* p_metadata,
                                  uint16_t addr_6502) {
  void* p_host_ptr = jit_metadata_get_host_block_address(p_metadata, addr_6502);
  void* p_jit_ptr = jit_metadata_get_host_jit_ptr(p_metadata, addr_6502);

  (void) p_host_ptr;
  assert(p_jit_ptr != p_host_ptr);

  if (p_jit_ptr == p_metadata->p_jit_ptr_no_code) {
    return 0;
  }
  /* Need to explicitly handle dynamic opcodes. The expectation is that they
   * always show as self-modified. We can't rely on the JIT code bytes in memory
   * on ARM64, because of the way the invalidation write works.
   */
  if (p_jit_ptr == p_metadata->p_jit_ptr_dynamic) {
    return 1;
  }

  assert(p_metadata->code_blocks[addr_6502] != -1);

  return asm_jit_is_invalidated_code_at(p_jit_ptr);
}

uint16_t
jit_metadata_get_block_addr_from_host_pc(struct jit_metadata* p_metadata,
                                         void* p_host_pc) {
  size_t block_addr_6502;
  void* p_jit_base = p_metadata->p_jit_base;

  block_addr_6502 = (p_host_pc - p_jit_base);
  block_addr_6502 /= K_JIT_BYTES_PER_BYTE;

  assert(block_addr_6502 < k_6502_addr_space_size);

  return (uint16_t) block_addr_6502;
}

uint16_t
jit_metadata_get_6502_pc_from_host_pc(struct jit_metadata* p_metadata,
                                      void* p_host_pc) {
  uint16_t addr_6502;
  uint16_t ret_addr_6502;

  void* p_curr_jit_ptr = NULL;
  uint16_t host_block_6502 =
      jit_metadata_get_block_addr_from_host_pc(p_metadata, p_host_pc);
  int32_t code_block_6502 = jit_metadata_get_code_block(p_metadata,
                                                        host_block_6502);

  if (((uintptr_t) p_host_pc & (K_JIT_BYTES_PER_BYTE - 1)) == 0) {
    return host_block_6502;
  }

  assert(code_block_6502 != -1);
  for (addr_6502 = code_block_6502;
       (jit_metadata_get_code_block(p_metadata, addr_6502) ==
           code_block_6502);
       ++addr_6502) {
    void* p_jit_ptr = jit_metadata_get_host_jit_ptr(p_metadata, addr_6502);
    assert(!jit_metadata_is_jit_ptr_no_code(p_metadata, p_jit_ptr));
    if (jit_metadata_is_jit_ptr_dynamic(p_metadata, p_jit_ptr)) {
      continue;
    }
    if (p_jit_ptr > p_host_pc) {
      break;
    }
    if (p_jit_ptr != p_curr_jit_ptr) {
      p_curr_jit_ptr = p_jit_ptr;
      ret_addr_6502 = addr_6502;
    }
  }

  return ret_addr_6502;
}

void
jit_metadata_set_jit_ptr(struct jit_metadata* p_metadata,
                         uint16_t addr_6502,
                         uint32_t jit_ptr) {
  p_metadata->p_jit_ptrs[addr_6502] = jit_ptr;
}

void
jit_metadata_make_jit_ptr_no_code(struct jit_metadata* p_metadata,
                                  uint16_t addr_6502) {
  uint32_t jit_ptr = (uint32_t) (uintptr_t) p_metadata->p_jit_ptr_no_code;
  p_metadata->p_jit_ptrs[addr_6502] = jit_ptr;
}

void
jit_metadata_make_jit_ptr_dynamic(struct jit_metadata* p_metadata,
                                  uint16_t addr_6502) {
  uint32_t jit_ptr = (uint32_t) (uintptr_t) p_metadata->p_jit_ptr_dynamic;
  p_metadata->p_jit_ptrs[addr_6502] = jit_ptr;
}

void
jit_metadata_set_code_block(struct jit_metadata* p_jit_metadata,
                            uint16_t addr_6502,
                            int32_t code_block) {
  p_jit_metadata->code_blocks[addr_6502] = code_block;
}

void
jit_metadata_invalidate_jump_target(struct jit_metadata* p_metadata,
                                    uint16_t addr_6502) {
  void* p_jit_ptr = jit_metadata_get_host_block_address(p_metadata, addr_6502);
  asm_jit_invalidate_code_at(p_jit_ptr);
}

void
jit_metadata_invalidate_code(struct jit_metadata* p_metadata, uint16_t addr) {
  void* p_jit_ptr = jit_metadata_get_host_jit_ptr(p_metadata, addr);
  asm_jit_invalidate_code_at(p_jit_ptr);
}
