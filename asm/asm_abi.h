#ifndef BEEBJIT_ASM_ABI_H
#define BEEBJIT_ASM_ABI_H

struct bbc_options;
struct debug_struct;
struct memory_access;
struct state_6502;

struct asm_abi {
  /* This private member is at offset zero so that asm backends can reference
   * it efficiently (particularly x64).
   */
  void* p_util_private;

  struct state_6502* p_state_6502;

  /* asm functions (often trampolines to C functions). */
  void* p_debug_asm;
  void* p_interp_asm;

  /* C functions called out to. */
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
