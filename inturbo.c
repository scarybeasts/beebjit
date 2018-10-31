#include "inturbo.h"

#include "asm_x64.h"
#include "defs_6502.h"
#include "util.h"

#include <err.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const size_t k_inturbo_bytes_per_opcode = 32;
static void* k_inturbo_opcodes_addr = (void*) 0x40000000;
static void* k_inturbo_jump_table_addr = (void*) 0x3f000000;
static const size_t k_inturbo_jump_table_size = 4096;

struct inturbo_struct {
  unsigned char* p_inturbo_base;
  unsigned char* p_jump_table;
};

static void
inturbo_fill_tables(struct inturbo_struct* p_inturbo) {
  size_t i;

  struct util_buffer* p_buf = util_buffer_create();
  unsigned char* p_inturbo_base = p_inturbo->p_inturbo_base;
  unsigned char* p_inturbo_opcodes_ptr = p_inturbo_base;
  uint64_t* p_jump_table_ptr = (uint64_t*) p_inturbo->p_jump_table;

  /* Opcode pointers for the "next opcode" jump table. */
  for (i = 0; i < 256; ++i) {
    *p_jump_table_ptr =
        (uint64_t) (p_inturbo_base + (i * k_inturbo_bytes_per_opcode));
    p_jump_table_ptr++;
  }

  for (i = 0; i < 256; ++i) {
    unsigned char optype = g_opmodes[i];
    util_buffer_setup(p_buf, p_inturbo_opcodes_ptr, k_inturbo_bytes_per_opcode);

    switch (optype) {
    default:
      break;
    }

    /* Load next opcode from 6502 PC, increment 6502 PC, jump to correct next
     * asm opcode inturbo handler.
     */
    asm_x64_copy(p_buf,
                 asm_x64_inturbo_next_opcode,
                 asm_x64_inturbo_next_opcode_END,
                 0);

    p_inturbo_opcodes_ptr += k_inturbo_bytes_per_opcode;
  }

  util_buffer_destroy(p_buf);
}

struct inturbo_struct*
inturbo_create(struct state_6502* p_state_6502,
               struct memory_access* p_memory_access,
               struct bbc_timing* p_timing,
               struct bbc_options* p_options) {
  struct inturbo_struct* p_inturbo = malloc(sizeof(struct inturbo_struct));

  (void) p_state_6502;
  (void) p_memory_access;
  (void) p_timing;
  (void) p_options;

  if (p_inturbo == NULL) {
    errx(1, "couldn't allocate inturbo_struct");
  }
  (void) memset(p_inturbo, '\0', sizeof(struct inturbo_struct));

  p_inturbo->p_inturbo_base = util_get_guarded_mapping(
      k_inturbo_opcodes_addr,
      256 * k_inturbo_bytes_per_opcode);
  util_make_mapping_read_write_exec(
      p_inturbo->p_inturbo_base,
      256 * k_inturbo_bytes_per_opcode);

  p_inturbo->p_jump_table = util_get_guarded_mapping(k_inturbo_jump_table_addr,
                                                     k_inturbo_jump_table_size);

  inturbo_fill_tables(p_inturbo);

  return p_inturbo;
}

void
inturbo_destroy(struct inturbo_struct* p_inturbo) {
  util_free_guarded_mapping(p_inturbo->p_inturbo_base,
                            256 * k_inturbo_bytes_per_opcode);
  util_free_guarded_mapping(p_inturbo->p_jump_table, k_inturbo_jump_table_size);
  free(p_inturbo);
}

void
inturbo_enter(struct inturbo_struct* p_inturbo) {
  (void) p_inturbo;
}

void
inturbo_async_timer_tick(struct inturbo_struct* p_inturbo) {
  (void) p_inturbo;
}
