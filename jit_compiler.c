#include "jit_compiler.h"

#include "util.h"

#include <err.h>
#include <stdlib.h>
#include <string.h>

struct jit_compiler {
  uint8_t* p_read_mem;
};

struct jit_compiler*
jit_compiler_create(uint8_t* p_read_mem) {
  struct jit_compiler* p_compiler = malloc(sizeof(struct jit_compiler));
  if (p_compiler == NULL) {
    errx(1, "cannot alloc jit_compiler");
  }
  (void) memset(p_compiler, '\0', sizeof(struct jit_compiler));

  p_compiler->p_read_mem = p_read_mem;

  return p_compiler;
}

void
jit_compiler_destroy(struct jit_compiler* p_compiler) {
  free(p_compiler);
}

void
jit_compiler_compile_block(struct jit_compiler* p_compiler,
                           struct util_buffer* p_buf,
                           uint16_t addr_6502) {
  (void) p_compiler;
  (void) addr_6502;

  util_buffer_add_2b(p_buf, 0x0f, 0x0b);
}
