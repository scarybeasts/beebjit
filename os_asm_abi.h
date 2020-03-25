#ifndef BEEBJIT_OS_ASM_ABI_H
#define BEEBJIT_OS_ASM_ABI_H

#if defined(WIN32)

#define REG_PARAM1         rcx
#define REG_PARAM1_32      ecx
#define REG_PARAM2         rdx
#define REG_PARAM2_32      edx
#define REG_PARAM3         r8
#define REG_PARAM3_32      r8d
#define REG_PARAM4         r9
#define REG_PARAM4_32      r9d

#else

/* Everything else(?) is AMD64, including Linux, FreeBSD, Mac OS X, ... */
#define REG_PARAM1         rdi
#define REG_PARAM1_32      edi
#define REG_PARAM2         rsi
#define REG_PARAM2_32      esi
#define REG_PARAM3         rdx
#define REG_PARAM3_32      edx
#define REG_PARAM4         rcx
#define REG_PARAM4_32      ecx

#endif



#endif /* BEEBJIT_OS_ASM_ABI_H */
