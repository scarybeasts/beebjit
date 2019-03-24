#ifndef BEEBJIT_MEMORY_ACCESS_H
#define BEEBJIT_MEMORY_ACCESS_H

#include <stdint.h>

struct memory_access {
  uint8_t* p_mem_read;
  uint8_t* p_mem_write;

  void* p_callback_obj;
  int (*memory_is_ram)(void* p, uint16_t addr);
  uint16_t (*memory_read_needs_callback_above)(void* p);
  uint16_t (*memory_write_needs_callback_above)(void* p);
  int (*memory_read_needs_callback)(void* p, uint16_t addr);
  int (*memory_write_needs_callback)(void* p, uint16_t addr);

  uint8_t (*memory_read_callback)(void* p, uint16_t addr);
  void (*memory_write_callback)(void* p, uint16_t addr, uint8_t val);
};

#endif /* BEEBJIT_MEMORY_ACCESS_H */
