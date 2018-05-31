#include "debug.h"

#include "jit.h"
#include "opdefs.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const size_t k_max_opcode_len = 16;
static const size_t k_max_extra_len = 32;

static void
debug_print_opcode(char* buf,
                   size_t buf_len,
                   unsigned char opcode,
                   unsigned char operand1,
                   unsigned char operand2) {
  unsigned char opmode = g_opmodes[opcode];
  const char* opname = g_p_opnames[g_optypes[opcode]];
  switch (opmode) {
  case k_nil:
    snprintf(buf, buf_len, "%s", opname);
    break;
  case k_imm:
    snprintf(buf, buf_len, "%s #$%.2x", opname, operand1);
    break;
  case k_zpg:
    snprintf(buf, buf_len, "%s $%.2x", opname, operand1);
    break;
  case k_abs:
    snprintf(buf, buf_len, "%s $%.2x%.2x", opname, operand2, operand1);
    break;
  case k_zpx:
    snprintf(buf, buf_len, "%s $%.2x,X", opname, operand1);
    break;
  case k_zpy:
    snprintf(buf, buf_len, "%s $%.2x,Y", opname, operand1);
    break;
  case k_abx:
    snprintf(buf, buf_len, "%s $%.2x%.2x,X", opname, operand2, operand1);
    break;
  case k_aby:
    snprintf(buf, buf_len, "%s $%.2x%.2x,Y", opname, operand2, operand1);
    break;
  case k_idx:
    snprintf(buf, buf_len, "%s ($%.2x,X)", opname, operand1);
    break;
  case k_idy:
    snprintf(buf, buf_len, "%s ($%.2x),Y", opname, operand1);
    break;
  case k_ind:
    snprintf(buf, buf_len, "%s ($%.2x%.2x)", opname, operand2, operand1);
    break;
  default:
    snprintf(buf, buf_len, "%s: %.2x", opname, opcode);
    break;
  }
}

static void
debug_get_addr(char* p_buf,
               size_t buf_len,
               unsigned char opcode,
               unsigned char operand1,
               unsigned char operand2,
               unsigned char x_6502,
               unsigned char y_6502,
               unsigned char* p_mem) {
  unsigned char opmode = g_opmodes[opcode];
  uint16_t addr;
  switch (opmode) {
  case k_zpg:
    addr = operand1;
    break;
  case k_zpx:
    addr = (unsigned char) (operand1 + x_6502);
    break;
  case k_zpy:
    addr = (unsigned char) (operand1 + y_6502);
    break;
  case k_abs:
    addr = (uint16_t) (operand1 + (operand2 << 8));
    break;
  case k_abx:
    addr = (uint16_t) (operand1 + (operand2 << 8) + x_6502);
    break;
  case k_aby:
    addr = (uint16_t) (operand1 + (operand2 << 8) + y_6502);
    break;
  case k_idx:
    addr = p_mem[(unsigned char) (operand1 + x_6502 + 1)];
    addr <<= 8;
    addr |= p_mem[(unsigned char) (operand1 + x_6502)];
    break;
  case k_idy:
    addr = p_mem[(unsigned char) (operand1 + 1)];
    addr <<= 8;
    addr |= p_mem[operand1];
    addr = (uint16_t) (addr + y_6502);
    break;
  default:
    return;
    break;
  }
  snprintf(p_buf, buf_len, "[addr=%.4x val=%.2x]", addr, p_mem[addr]);
}

static void
debug_get_branch(char* p_buf,
                 size_t buf_len,
                 unsigned char opcode,
                 struct jit_struct* p_jit) {
  int taken = -1;
  switch (g_optypes[opcode]) {
  case k_bpl:
    taken = !p_jit->fn_6502;
    break;
  case k_bmi:
    taken = p_jit->fn_6502;
    break;
  case k_bvc:
    taken = !p_jit->fo_6502;
    break;
  case k_bvs:
    taken = p_jit->fo_6502;
    break;
  case k_bcc:
    taken = !p_jit->fc_6502;
    break;
  case k_bcs:
    taken = p_jit->fc_6502;
    break;
  case k_bne:
    taken = !p_jit->fz_6502;
    break;
  case k_beq:
    taken = p_jit->fz_6502;
    break;
  default:
    break;
  }
  if (taken == 0) {
    snprintf(p_buf, buf_len, "[not taken]");
  } else if (taken == 1) {
    snprintf(p_buf, buf_len, "[taken]");
  }
}

void
debug_jit_callback(struct jit_struct* p_jit) {
  char opcode_buf[k_max_opcode_len];
  char extra_buf[k_max_extra_len];
  char flags_buf[9];
  unsigned char* p_mem = p_jit->p_mem;
  uint16_t ip_6502 = p_jit->ip_6502;
  unsigned char a_6502 = p_jit->a_6502;
  unsigned char x_6502 = p_jit->x_6502;
  unsigned char y_6502 = p_jit->y_6502;
  unsigned char s_6502 = p_jit->s_6502;
  unsigned char opcode = p_mem[ip_6502];
  unsigned char operand1 = p_mem[((ip_6502 + 1) & 0xffff)];
  unsigned char operand2 = p_mem[((ip_6502 + 2) & 0xffff)];
  extra_buf[0] = '\0';
  debug_get_addr(extra_buf,
                 sizeof(extra_buf),
                 opcode,
                 operand1,
                 operand2,
                 x_6502,
                 y_6502,
                 p_mem);
  debug_get_branch(extra_buf, sizeof(extra_buf), opcode, p_jit);

  debug_print_opcode(opcode_buf,
                     sizeof(opcode_buf),
                     opcode,
                     operand1,
                     operand2);

  memset(flags_buf, ' ', 8);
  flags_buf[8] = '\0';
  if (p_jit->fc_6502) {
    flags_buf[0] = 'C';
  }
  if (p_jit->fz_6502) {
    flags_buf[1] = 'Z';
  }
  if (p_jit->f_6502 & 4) {
    flags_buf[2] = 'I';
  }
  if (p_jit->f_6502 & 8) {
    flags_buf[3] = 'D';
  }
  flags_buf[5] = '1';
  if (p_jit->fo_6502) {
    flags_buf[6] = 'O';
  }
  if (p_jit->fn_6502) {
    flags_buf[7] = 'N';
  }

  printf("%.4x: %-16s [A=%.2x X=%.2x Y=%.2x S=%.2x F=%s] %s\n",
         ip_6502,
         opcode_buf,
         a_6502,
         x_6502,
         y_6502,
         s_6502,
         flags_buf,
         extra_buf);
  fflush(stdout);
}
