#include "debug.h"

#include "jit.h"
#include "opdefs.h"

#include <ctype.h>
#include <err.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*sighandler_t)(int);

static const size_t k_max_opcode_len = 16;
static const size_t k_max_extra_len = 32;
static const size_t k_max_input_len = 256;

static int debug_inited = 0;
static int debug_break_addr = -1;
static int debug_running = 0;
static int debug_running_print = 0;

static void
int_handler(int signum) {
  (void) signum;
  debug_running = 0;
}

static void
debug_init() {
  sighandler_t ret = signal(SIGINT, int_handler);
  if (ret == SIG_ERR) {
    errx(1, "signal failed");
  }
  debug_inited = 1;
}

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
  char input_buf[k_max_input_len];
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

  if (!debug_inited) {
    debug_init();
  }

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

  if (debug_running && ip_6502 != debug_break_addr && !debug_running_print) {
    return;
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

  if (debug_running && ip_6502 != debug_break_addr) {
    return;
  }
  debug_running = 0;

  while (1) {
    char* input_ret;
    size_t i;
    int parse_int;
    uint16_t parse_addr;

    printf("(6502db) ");
    fflush(stdout);

    input_ret = fgets(input_buf, sizeof(input_buf), stdin);
    if (input_ret == NULL) {
      errx(1, "fgets failed");
    }
    for (i = 0; i < sizeof(input_buf); ++i) {
      char c = tolower(input_buf[i]);
      if (c == '\n') {
        c = 0;
      }
      input_buf[i] = c;
    }

    if (!strcmp(input_buf, "q")) {
      exit(0);
    } else if (!strcmp(input_buf, "p")) {
      debug_running_print = !debug_running_print;
    } else if (!strcmp(input_buf, "s")) {
      break;
    } else if (!strcmp(input_buf, "c")) {
      debug_running = 1;
      break;
    } else if (sscanf(input_buf, "b %x", &parse_int) == 1) {
      debug_break_addr = parse_int;
    } else if (!strcmp(input_buf, "d")) {
      debug_break_addr = -1;
    } else if (sscanf(input_buf, "m %x", &parse_int) == 1) {
      parse_addr = parse_int;
      printf("%04x:", parse_addr);
      for (i = 0; i < 16; ++i) {
        printf(" %02x", p_mem[parse_addr]);
        parse_addr++;
      }
      printf(" ");
      parse_addr = parse_int;
      for (i = 0; i < 16; ++i) {
        char c = p_mem[parse_addr];
        if (!isprint(c)) {
          c = '.';
        }
        printf("%c", c);
        parse_addr++;
      }
      printf("\n");
      fflush(stdout);
    } else {
      printf("???\n");
      fflush(stdout);
    }
  }
}
