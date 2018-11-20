#ifndef BEEBJIT_BBC_OPTIONS_H
#define BEEBJIT_BBC_OPTIONS_H

#include <stddef.h>
#include <stdint.h>

struct bbc_options {
  /* External options. */
  const char* p_opt_flags;
  const char* p_log_flags;

  /* Internal options, callbacks, etc. */
  void* p_debug_callback_object;
  int (*debug_subsystem_active)(void* p);
  int (*debug_active_at_addr)(void* p, uint16_t addr);
  void* (*debug_callback)(void* p, uint16_t irq_vector);
};

#endif /* BEEBJIT_BBC_OPTIONS_H */
