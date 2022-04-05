#if defined(__x86_64__)
#include "x64/asm_jit_x64.c"
#elif defined(__aarch64__)
#include "arm64/asm_jit_arm64.c"
#else
#include "null/asm_jit_null.c"
#endif
