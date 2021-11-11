#if defined(__x86_64__)
#include "x64/asm_inturbo_x64.S"
#elif defined(__aarch64__) && !__APPLE__
#include "arm64/asm_inturbo_arm64.S"
#else
/* No asm. */
#endif
