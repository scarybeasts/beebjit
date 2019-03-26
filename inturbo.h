#ifndef BEEBJIT_INTURBO_H
#define BEEBJIT_INTURBO_H

struct cpu_driver;
struct cpu_driver_funcs;

struct cpu_driver* inturbo_create(struct cpu_driver_funcs* p_funcs);

#endif /* BEEBJIT_INTURBO_H */
