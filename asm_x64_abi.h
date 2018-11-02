#ifndef BEEBJIT_ASM_X64_ABI_H
#define BEEBJIT_ASM_X64_ABI_H

struct bbc_options;
struct state_6502;

enum {
  k_asm_x64_abi_size = (5 * 8),
  k_asm_x64_abi_offset_util_private = 0,
  k_asm_x64_abi_offset_util_debug = 8,
  k_asm_x64_abi_offset_state_6502 = 16,
};

struct asm_x64_abi {
  void* p_util_private;
  void* p_util_debug;

  struct state_6502* p_state_6502;

  void* p_debug_callback;
  void* p_debug_object;
};

void asm_x64_abi_init(struct asm_x64_abi* p_abi,
                      struct bbc_options* p_options,
                      struct state_6502* p_state_6502);

#endif /* BEEBJIT_ASM_X64_ABI_H */
