#ifndef BEEBJIT_INTURBO_H
#define BEEBJIT_INTURBO_H

struct cpu_driver;
struct cpu_driver_funcs;
struct interp_struct;
struct inturbo_struct;

struct cpu_driver* inturbo_create(struct cpu_driver_funcs* p_funcs);
void inturbo_set_interp(struct inturbo_struct* p_inturbo,
                        struct interp_struct* p_interp);
/* If set, ret mode ends each inturbo instruction with a ret instead of
 * loading and jumping to the next instruction.
 * Used when supporting JIT.
 */
void inturbo_set_ret_mode(struct inturbo_struct* p_inturbo);
/* If set, do JIT code write invalidation for memory writes.
 * Used when supporting JIT.
 */
void inturbo_set_do_write_invalidation(struct inturbo_struct* p_inturbo);

#endif /* BEEBJIT_INTURBO_H */
