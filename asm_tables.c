#include "asm_tables.h"

#include "util.h"

#include <stddef.h>

static void* k_asm_tables_addr = (void*) 0x50000000;
static const size_t k_asm_tables_size = 4096;

static const int k_asm_table_offset_6502_flags_to_x64 = 0;
static const int k_asm_table_offset_6502_flags_to_mask = 0x100;
static const int k_asm_table_offset_x64_flags_to_6502 = 0x200;

static int s_inited;

void
asm_tables_init() {
  size_t i;
  unsigned char* p_dst;

  if (s_inited) {
    return;
  }
  s_inited = 1;


  (void) util_get_guarded_mapping(k_asm_tables_addr, k_asm_tables_size);

  p_dst = (k_asm_tables_addr + k_asm_table_offset_6502_flags_to_x64);
  for (i = 0; i < 0x100; ++i) {
    unsigned char val = 0;
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

  p_dst = (k_asm_tables_addr + k_asm_table_offset_6502_flags_to_mask);
  for (i = 0; i < 0x100; ++i) {
    unsigned char val = (i & 0x0C);
    *p_dst++ = val;
  }

  p_dst = (k_asm_tables_addr + k_asm_table_offset_x64_flags_to_6502);
  for (i = 0; i < 0x100; ++i) {
    unsigned char val = 0;
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
}
