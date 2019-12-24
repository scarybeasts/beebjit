#ifndef BEEBJIT_BBC_OPTIONS_H
#define BEEBJIT_BBC_OPTIONS_H

#include <stddef.h>
#include <stdint.h>

struct cpu_driver;

struct bbc_options {
  /* External options. */
  const char* p_opt_flags;
  const char* p_log_flags;
  int accurate;
  int test_map;

  /* Internal options, callbacks, etc. */
  struct debug_struct* p_debug_object;
  int (*debug_subsystem_active)(void* p);
  int (*debug_active_at_addr)(void* p, uint16_t addr);
  void* (*debug_callback)(struct cpu_driver* p_cpu_driver, int do_irq);
};

#endif /* BEEBJIT_BBC_OPTIONS_H */
