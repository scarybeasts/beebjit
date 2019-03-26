#ifndef BEEBJIT_JIT_H
#define BEEBJIT_JIT_H

struct cpu_driver;
struct cpu_driver_funcs;

struct cpu_driver* jit_create(struct cpu_driver_funcs* p_funcs);

#endif /* BEEJIT_JIT_H */
