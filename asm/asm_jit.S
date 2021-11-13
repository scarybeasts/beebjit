#if defined(__x86_64__)
#include "x64/asm_jit_x64.S"
#elif defined(__aarch64__)
#include "arm64/asm_jit_arm64.S"
#else
/* No asm. */
#endif
