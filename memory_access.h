#ifndef BEEBJIT_MEMORY_ACCESS_H
#define BEEBJIT_MEMORY_ACCESS_H

#include <stdint.h>

struct asm_uop;

struct memory_access {
  uint8_t* p_mem_read;
  uint8_t* p_mem_write;

  void* p_callback_obj;
  int (*memory_is_always_ram)(void* p, uint16_t addr);
  uint16_t (*memory_read_needs_callback_from)(void* p);
  uint16_t (*memory_write_needs_callback_from)(void* p);
  int (*memory_read_needs_callback)(void* p, uint16_t addr);
  int (*memory_write_needs_callback)(void* p, uint16_t addr);

  uint8_t (*memory_read_callback)(void* p,
                                  uint16_t addr,
                                  uint16_t pc,
                                  int do_tick_callback);
  /* The memory write callback is permitted to change the ranges that require
   * callbacks. It returns non-zero if it did this.
   */
  int (*memory_write_callback)(void* p,
                               uint16_t addr,
                               uint8_t val,
                               uint16_t pc,
                               int do_tick_callback);

  /* JIT integration. */
  uint32_t (*memory_get_read_jit_encoding)(void* p,
                                           struct asm_uop* p_uops,
                                           uint32_t num_uops,
                                           uint16_t addr_6502);
  uint32_t (*memory_get_write_jit_encoding)(void* p,
                                            struct asm_uop* p_uops,
                                            uint32_t num_uops,
                                            uint16_t addr_6502);

  void* p_last_tick_callback_obj;
  void (*memory_client_last_tick_callback)(void* p);
};

#endif /* BEEBJIT_MEMORY_ACCESS_H */
