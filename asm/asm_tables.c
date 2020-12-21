#include "asm_tables.h"

#include "asm_defs_host.h"
#include "../os_alloc.h"

static const size_t k_asm_tables_size = 4096;

static int s_inited;

void
asm_tables_init() {
  size_t i;
  uint8_t* p_dst;

  if (s_inited) {
    return;
  }
  s_inited = 1;


  p_dst = (uint8_t*) K_ASM_TABLE_6502_FLAGS_TO_X64;
  (void) os_alloc_get_mapping(p_dst, k_asm_tables_size);

  for (i = 0; i < 0x100; ++i) {
    uint8_t val = 0;
    int zf = (i & 0x02);
    int nf = (i & 0x80);
    if (zf) {
      val |= 0x40;
    }
    if (nf) {
      val |= 0x80;
    }
    *p_dst++ = val;
  }

  p_dst = (uint8_t*) K_ASM_TABLE_6502_FLAGS_TO_MASK;
  for (i = 0; i < 0x100; ++i) {
    uint8_t val = (i & 0x0C);
    *p_dst++ = val;
  }

  p_dst = (uint8_t*) K_ASM_TABLE_X64_FLAGS_TO_6502;
  for (i = 0; i < 0x100; ++i) {
    uint8_t val = 0;
    int zf = (i & 0x40);
    int nf = (i & 0x80);
    if (zf) {
      val |= 0x02;
    }
    if (nf) {
      val |= 0x80;
    }
    *p_dst++ = val;
  }

  p_dst = (uint8_t*) K_ASM_TABLE_PAGE_CROSSING_CYCLE_INV;
  for (i = 0; i < 0x200; ++i) {
    *p_dst++ = (i < 0x100);
  }

  p_dst = (uint8_t*) K_ASM_TABLE_OF_TO_6502;
  *p_dst++ = 0;
  *p_dst++ = 0x40;
}
