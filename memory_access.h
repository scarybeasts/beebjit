#ifndef BEEBJIT_MEMORY_ACCESS_H
#define BEEBJIT_MEMORY_ACCESS_H

#include <stdint.h>

struct memory_access {
  unsigned char* p_mem_read;
  unsigned char* p_mem_write;

  void* p_callback_obj;
  int (*memory_is_ram)(void* p, uint16_t addr);
  uint16_t (*memory_read_needs_callback_above)(void* p);
  uint16_t (*memory_write_needs_callback_above)(void* p);

  unsigned char (*memory_read_callback)(void* p, uint16_t addr);
  void (*memory_write_callback)(void* p, uint16_t addr, unsigned char val);
};

#endif /* BEEBJIT_MEMORY_ACCESS_H */
