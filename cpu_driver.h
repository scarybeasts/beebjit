#ifndef BEEBJIT_CPU_DRIVER_H
#define BEEBJIT_CPU_DRIVER_H

#include "asm_x64_abi.h"

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

struct cpu_driver_funcs {
  void (*init)(struct cpu_driver* p_cpu_driver);
  void (*destroy)(struct cpu_driver* p_cpu_driver);
  int (*enter)(struct cpu_driver* p_cpu_driver);
  void (*exit)(struct cpu_driver* p_cpu_driver, uint32_t exit_value);
  int (*has_exited)(struct cpu_driver* p_cpu_driver);
  uint32_t (*get_exit_value)(struct cpu_driver* p_cpu_driver);

  void (*memory_range_invalidate)(struct cpu_driver* p_cpu_driver,
                                  uint16_t addr,
                                  uint16_t len);
  char* (*get_address_info)(struct cpu_driver* p_cpu_driver, uint16_t addr);
  void (*get_custom_counters)(struct cpu_driver* p_cpu_driver,
                              uint64_t* p_c1,
                              uint64_t* p_c2);
};

struct cpu_driver {
  struct asm_x64_abi abi;

  struct memory_access* p_memory_access;
  struct timing_struct* p_timing;
  struct bbc_options* p_options;

  struct cpu_driver_funcs* p_funcs;

  int has_exited;
  uint32_t exit_value;
};

struct cpu_driver* cpu_driver_alloc(int mode,
                                    struct state_6502* p_state_6502,
                                    struct memory_access* p_memory_access,
                                    struct timing_struct* p_timing,
                                    struct bbc_options* p_options);

#endif /* BEEBJIT_CPU_DRIVER_H */
