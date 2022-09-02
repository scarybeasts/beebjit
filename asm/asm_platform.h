#ifndef BEEBJIT_ASM_PLATFORM_H
#define BEEBJIT_ASM_PLATFORM_H

#if __APPLE__
#define ASM_SYM(x) _ ## x
#else
#define ASM_SYM(x) x
#endif

#if __APPLE__
#if defined(__x86_64__)
/* Apple macOS on x64. System mappings spill upwards from the 0x00400000 binary
 * placement we chose.
 * Place our mappings plenty higher to avoid collisions, but within +2GB jump
 * range.
 */
#define K_BBC_MEM_RAW_ADDR                 0x70008000
#define K_JIT_ADDR                         0x75000000
#define K_INTURBO_ADDR                     0x76000000
#define K_ASM_TABLE_ADDR                   0x77000000
#define K_JIT_TRAMPOLINES_ADDR             0x77800000
#else
/* Apple macOS on ARM64. 64-bit addresses strictly required. <4GB unmappable. */
#define K_BBC_MEM_RAW_ADDR                 0x30f008000
#define K_JIT_ADDR                         0x300000000
#define K_INTURBO_ADDR                     0x400000000
#define K_ASM_TABLE_ADDR                   0x0          /* Unused om ARM64. */
#endif
#else
/* Linux and Windows. */
#define K_BBC_MEM_RAW_ADDR                 0x0f008000
#define K_JIT_ADDR                         0x06000000
#define K_INTURBO_ADDR                     0x07000000
#define K_ASM_TABLE_ADDR                   0x50000000
#define K_JIT_TRAMPOLINES_ADDR             0x80000000
#endif

#endif /* BEEBJIT_ASM_PLATFORM_H */
