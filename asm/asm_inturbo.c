#if defined(__x86_64__)
#include "x64/asm_inturbo_x64.c"
#elif defined(__aarch64__) && !__APPLE__
#include "arm64/asm_inturbo_arm64.c"
#else
#include "null/asm_inturbo_null.c"
#endif
