#ifndef BEEBJIT_OS_COMPILER_HINTS_H
#define BEEBJIT_OS_COMPILER_HINTS_H

/* Hints for laying out the hottest loops, so that the common case runs
 * straight through without taken branches.
 */
#if defined(__GNUC__) || defined(__clang__)
#define OS_LIKELY(x) __builtin_expect(!!(x), 1)
#define OS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define OS_LIKELY(x) (x)
#define OS_UNLIKELY(x) (x)
#endif

#endif /* BEEBJIT_OS_COMPILER_HINTS_H */
