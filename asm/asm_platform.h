#ifndef BEEBJIT_ASM_PLATFORM_H
#define BEEBJIT_ASM_PLATFORM_H

#if __APPLE__
#define ASM_SYM(x) _ ## x
#else
#define ASM_SYM(x) x
#endif

#endif /* BEEBJIT_ASM_PLATFORM_H */
