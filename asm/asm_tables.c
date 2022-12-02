#include "asm_tables.h"

#include "asm_defs_host.h"
#include "asm_tables_defs.h"
#include "asm_jit.h"
#include "../os_alloc.h"

void* g_p_asm_tables_base = NULL;
static const size_t k_asm_tables_size = 4096;

static int s_inited;

void
asm_tables_init() {
  struct os_alloc_mapping* p_mapping;
  uint8_t* p_base;
  size_t i;
  uint8_t* p_dst;

  if (s_inited) {
    return;
  }
  s_inited = 1;

  /* TODO: this is temporary. This hack makes sure we only create the tables if
   * the x64 backend is in use. They should be initialized somewhere in the x64
   * specific backend.
   */
  if (!asm_jit_uses_indirect_mappings()) {
    return;
  }

  /* Little dance to avoid GCC 11 bug with -Werror=stringop-overflow. */
  if (g_p_asm_tables_base == NULL) {
    g_p_asm_tables_base = (void*) K_ASM_TABLE_ADDR;
  }

  p_mapping = os_alloc_get_mapping(g_p_asm_tables_base, k_asm_tables_size);
  p_base = os_alloc_get_mapping_addr(p_mapping);

  p_dst = (p_base + K_ASM_TABLE_6502_FLAGS_TO_X64_OFFSET);
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

  p_dst = (p_base + K_ASM_TABLE_6502_FLAGS_TO_MASK_OFFSET);
  for (i = 0; i < 0x100; ++i) {
    uint8_t val = (i & 0x0C);
    *p_dst++ = val;
  }

  p_dst = (p_base + K_ASM_TABLE_X64_FLAGS_TO_6502_OFFSET);
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

  p_dst = (p_base + K_ASM_TABLE_PAGE_WRAP_CYCLE_INV_OFFSET);
  for (i = 0; i < 0x200; ++i) {
    *p_dst++ = (i < 0x100);
  }

  p_dst = (p_base + K_ASM_TABLE_OF_TO_6502_OFFSET);
  *p_dst++ = 0;
  *p_dst++ = 0x40;

  os_alloc_make_mapping_read_only(p_base, k_asm_tables_size);
}
