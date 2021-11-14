#ifndef BEEBJIT_ASM_PLATFORM_H
#define BEEBJIT_ASM_PLATFORM_H

#if __APPLE__
#define ASM_SYM(x) _ ## x
#else
#define ASM_SYM(x) x
#endif

#if __APPLE__ && !defined(__x86_64__)
/* Has to be >4GB. */
#define K_BBC_MEM_RAW_ADDR                 0x30f008000
#define K_JIT_ADDR                         0x300000000
#else
#define K_BBC_MEM_RAW_ADDR                 0x0f008000
#define K_JIT_ADDR                         0x06000000
#endif

#endif /* BEEBJIT_ASM_PLATFORM_H */
