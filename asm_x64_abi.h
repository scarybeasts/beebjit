#ifndef BEEBJIT_ASM_X64_ABI_H
#define BEEBJIT_ASM_X64_ABI_H

struct bbc_options;
struct state_6502;

struct asm_x64_abi {
  void* p_util_private;
  void* p_util_debug;

  struct state_6502* p_state_6502;

  void* p_private_callback;
  void* p_debug_callback;

  void* p_debug_object;
};

void asm_x64_abi_init(struct asm_x64_abi* p_abi,
                      struct bbc_options* p_options,
                      struct state_6502* p_state_6502);

#endif /* BEEBJIT_ASM_X64_ABI_H */
