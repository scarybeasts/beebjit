#include "jit_compiler.h"

#include "asm_x64_jit.h"
#include "defs_6502.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

struct jit_compiler {
  uint8_t* p_mem_read;
  struct util_buffer* p_buf;

  int32_t reg_a;
};

struct jit_opcode {
  int32_t opcode;
  int32_t value;
};

struct jit_opcode_pair {
  struct jit_opcode first_opcode;
  struct jit_opcode second_opcode;
};

static const int32_t k_value_unknown = -1;

enum {
  k_opcode_countdown = 0x100,
  k_opcode_FLAGA,
  k_opcode_FLAGX,
  k_opcode_FLAGY,
  k_opcode_LODA_IMM,
  k_opcode_STOA_IMM,
};

struct jit_compiler*
jit_compiler_create(uint8_t* p_mem_read) {
  struct jit_compiler* p_compiler = malloc(sizeof(struct jit_compiler));
  if (p_compiler == NULL) {
    errx(1, "cannot alloc jit_compiler");
  }
  (void) memset(p_compiler, '\0', sizeof(struct jit_compiler));

  p_compiler->p_mem_read = p_mem_read;

  return p_compiler;
}

void
jit_compiler_destroy(struct jit_compiler* p_compiler) {
  free(p_compiler);
}

static uint16_t
jit_compiler_get_opcodes(struct jit_compiler* p_compiler,
                         struct jit_opcode_pair* p_pair,
                         uint16_t addr_6502) {
  uint8_t byte;
  uint8_t opmode;
  uint8_t oplen;

  uint8_t* p_mem_read = p_compiler->p_mem_read;
  uint16_t addr_plus_1 = (addr_6502 + 1);
  uint16_t addr_plus_2 = (addr_6502 + 2);

  byte = p_mem_read[addr_6502];
  opmode = g_opmodes[byte];
  oplen = g_opmodelens[opmode];

  p_pair->first_opcode.opcode = byte;
  p_pair->second_opcode.opcode = -1;

  switch (byte) {
  case 0xA9: /* LDA imm */
    p_pair->first_opcode.opcode = k_opcode_LODA_IMM;
    p_pair->first_opcode.value = p_mem_read[addr_plus_1];
    p_pair->second_opcode.opcode = k_opcode_FLAGA;
    break;
  default:
    switch (opmode) {
    case k_abs:
      p_pair->first_opcode.value = ((p_mem_read[addr_plus_2] << 8) |
                                    p_mem_read[addr_plus_1]);
      break;
    default:
      assert(0);
      break;
    }
    break;
  }

  addr_6502 += oplen;
  return addr_6502;
}

static void
jit_compiler_emit_opcode(struct jit_compiler* p_compiler,
                         int opcode,
                         int value1,
                         int value2) {
  struct util_buffer* p_buf = p_compiler->p_buf;

  switch (opcode) {
  case k_opcode_FLAGA:
    asm_x64_emit_jit_FLAGA(p_buf);
    break;
  case k_opcode_LODA_IMM:
    asm_x64_emit_jit_LODA_IMM(p_buf, (uint8_t) value1);
    break;
  case k_opcode_STOA_IMM:
    asm_x64_emit_jit_STOA_IMM(p_buf, (uint16_t) value1, (uint8_t) value2);
    break;
  default:
    assert(0);
    break;
  }
}

static void
jit_compiler_process_opcode(struct jit_compiler* p_compiler,
                            struct jit_opcode* p_opcode) {
  int32_t opcode = p_opcode->opcode;
  int32_t value1 = p_opcode->value;
  int32_t value2 = 0;

  switch (opcode) {
  case k_opcode_LODA_IMM:
    p_compiler->reg_a = value1;
    break;
  case 0x8D: /* STA abs */
    if (p_compiler->reg_a != k_value_unknown) {
      opcode = k_opcode_STOA_IMM;
      value2 = p_compiler->reg_a;
    }
    break;
  }
  jit_compiler_emit_opcode(p_compiler, opcode, value1, value2);
}

void
jit_compiler_compile_block(struct jit_compiler* p_compiler,
                           struct util_buffer* p_buf,
                           uint16_t addr_6502) {
  size_t i;
  struct jit_opcode_pair opcode_pair;

  opcode_pair.first_opcode.opcode = -1;
  opcode_pair.first_opcode.value = -1;
  opcode_pair.second_opcode.opcode = -1;
  opcode_pair.second_opcode.value = -1;

  p_compiler->p_buf = p_buf;
  p_compiler->reg_a = k_value_unknown;

  for (i = 0; i < 2; ++i ) {
    addr_6502 = jit_compiler_get_opcodes(p_compiler, &opcode_pair, addr_6502);

    jit_compiler_process_opcode(p_compiler, &opcode_pair.first_opcode);
    if (opcode_pair.second_opcode.opcode != -1) {
      jit_compiler_process_opcode(p_compiler, &opcode_pair.second_opcode);
    }
  }
}
