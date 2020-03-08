#if (defined __APPLE__) && (defined __MACH__)

#define SECTION_RODATA .section __DATA,__const

#elif defined __linux__

#define SECTION_RODATA .section rodata

#else

#error unknown platform

#endif
