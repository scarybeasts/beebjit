#ifndef BEEBJIT_ASM_ABI_H
#define BEEBJIT_ASM_ABI_H

struct bbc_options;
struct debug_struct;
struct memory_access;
struct state_6502;

enum {
  k_asm_abi_size = (6 * 8),
  k_asm_abi_offset_util_private = 0,
  k_asm_abi_offset_state_6502 = 8,
};

struct asm_abi {
  void* p_util_private;

  struct state_6502* p_state_6502;

  void* p_debug_callback;
  struct debug_struct* p_debug_object;

  void* p_interp_callback;
  void* p_interp_object;
};

void asm_abi_init(struct asm_abi* p_abi,
                  struct memory_access* p_memory_access,
                  struct bbc_options* p_options,
                  struct state_6502* p_state_6502);

#endif /* BEEBJIT_ASM_ABI_H */
