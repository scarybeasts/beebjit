#ifndef BEEBJIT_CPU_DRIVER_H
#define BEEBJIT_CPU_DRIVER_H

#include "asm/asm_abi.h"

#include <stdint.h>

struct bbc_options;
struct cpu_driver;
struct memory_access;
struct state_6502;
struct timing_struct;

enum {
  k_cpu_mode_interp = 1,
  k_cpu_mode_inturbo = 2,
  k_cpu_mode_jit = 3,
};

enum {
  k_cpu_flag_exited = 1,
  k_cpu_flag_soft_reset = 2,
  k_cpu_flag_hard_reset = 4,
  k_cpu_flag_replay = 8,
};

struct cpu_driver_funcs {
  void (*init)(struct cpu_driver* p_cpu_driver);
  void (*destroy)(struct cpu_driver* p_cpu_driver);
  void (*set_reset_callback)(struct cpu_driver* p_cpu_driver,
                             void (*do_reset_callback)(void* p, uint32_t flags),
                             void* p_do_reset_callback_object);
  void (*set_memory_written_callback)(struct cpu_driver* p_cpu_driver,
                                      void (*memory_written_callback)(void* p),
                                      void* p_memory_written_callback_object);
  int (*enter)(struct cpu_driver* p_cpu_driver);
  void (*apply_flags)(struct cpu_driver* p_cpu_driver,
                      uint32_t flags_set,
                      uint32_t flags_clear);
  uint32_t (*get_flags)(struct cpu_driver* p_cpu_driver);
  uint32_t (*get_exit_value)(struct cpu_driver* p_cpu_driver);
  void (*set_exit_value)(struct cpu_driver* p_cpu_driver, uint32_t exit_value);

  void (*memory_range_invalidate)(struct cpu_driver* p_cpu_driver,
                                  uint16_t addr,
                                  uint32_t len);
  char* (*get_address_info)(struct cpu_driver* p_cpu_driver, uint16_t addr);
  void (*get_custom_counters)(struct cpu_driver* p_cpu_driver,
                              uint64_t* p_c1,
                              uint64_t* p_c2);
  void (*get_opcode_maps)(struct cpu_driver* p_cpu_driver,
                          uint8_t** p_out_optypes,
                          uint8_t** p_out_opmodes,
                          uint8_t** p_out_opmem,
                          uint8_t** p_out_opcycles);
  void (*housekeeping_tick)(struct cpu_driver* p_cpu_driver);
};

struct cpu_driver_extra {
  struct memory_access* p_memory_access;
  struct timing_struct* p_timing;
  struct bbc_options* p_options;
  int32_t type;
};

struct cpu_driver {
  struct asm_abi abi;

  /* Per-driver-type functions. */
  struct cpu_driver_funcs* p_funcs;

  /* A few non-JIT-referenced fields stashed away in order to provide more
   * fields at <= 127 bytes offset from the start of the structure. Such fields
   * can be more efficiently referenced by some of the backends (x64).
   */
  struct cpu_driver_extra* p_extra;

  uint32_t flags;
  uint32_t exit_value;
  void (*do_reset_callback)(void* p, uint32_t flags);
  void* p_do_reset_callback_object;
};

struct cpu_driver* cpu_driver_alloc(int mode,
                                    int is_65c12,
                                    struct state_6502* p_state_6502,
                                    struct memory_access* p_memory_access,
                                    struct timing_struct* p_timing,
                                    struct bbc_options* p_options);

void cpu_driver_init(struct cpu_driver* p_driver);

#endif /* BEEBJIT_CPU_DRIVER_H */
