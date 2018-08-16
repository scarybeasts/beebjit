#define _GNU_SOURCE /* For qsort_r() */

#include "debug.h"

#include "bbc.h"
#include "opdefs.h"
#include "state.h"

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
enum {
  k_max_break = 16,
};
enum {
  k_max_input_len = 256,
};

struct debug_struct {
  struct bbc_struct* p_bbc;
  /* Stats. */
  int stats;
  size_t count_addr[k_bbc_addr_space_size];
  size_t count_opcode[k_6502_op_num_opcodes];
  size_t count_optype[k_6502_op_num_types];
  size_t count_opmode[k_6502_op_num_modes];
};

/* TODO: move into debug_struct! */
static int debug_inited = 0;
static int debug_break_exec[k_max_break];
static int debug_break_mem_low[k_max_break];
static int debug_break_mem_high[k_max_break];
static int debug_running = 0;
static int debug_running_print = 0;
static int debug_break_opcodes[256];

static char debug_old_input_buf[k_max_input_len];

static void
int_handler(int signum) {
  (void) signum;
  debug_running = 0;
}

struct debug_struct*
debug_create(struct bbc_struct* p_bbc) {
  struct debug_struct* p_debug = malloc(sizeof(struct debug_struct));
  if (p_debug == NULL) {
    errx(1, "couldn't allocate debug_struct");
  }
  memset(p_debug, '\0', sizeof(struct debug_struct));

  debug_running = bbc_get_run_flag(p_bbc);
  debug_running_print = bbc_get_print_flag(p_bbc);

  p_debug->p_bbc = p_bbc;

  return p_debug;
}

void
debug_destroy(struct debug_struct* p_debug) {
  free(p_debug);
}

static void
debug_init() {
  size_t i;
  sighandler_t ret = signal(SIGINT, int_handler);
  if (ret == SIG_ERR) {
    errx(1, "signal failed");
  }

  for (i = 0; i < k_max_break; ++i) {
    debug_break_exec[i] = -1;
    debug_break_mem_low[i] = -1;
    debug_break_mem_high[i] = -1;
  }

  debug_inited = 1;
}

static void
debug_print_opcode(char* buf,
                   size_t buf_len,
                   unsigned char opcode,
                   unsigned char operand1,
                   unsigned char operand2,
                   uint16_t reg_pc) {
  unsigned char opmode = g_opmodes[opcode];
  const char* opname = g_p_opnames[g_optypes[opcode]];
  uint16_t addr = operand1 | (operand2 << 8);
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
    snprintf(buf, buf_len, "%s $%.4x", opname, addr);
    break;
  case k_zpx:
    snprintf(buf, buf_len, "%s $%.2x,X", opname, operand1);
    break;
  case k_zpy:
    snprintf(buf, buf_len, "%s $%.2x,Y", opname, operand1);
    break;
  case k_abx:
    snprintf(buf, buf_len, "%s $%.4x,X", opname, addr);
    break;
  case k_aby:
    snprintf(buf, buf_len, "%s $%.4x,Y", opname, addr);
    break;
  case k_idx:
    snprintf(buf, buf_len, "%s ($%.2x,X)", opname, operand1);
    break;
  case k_idy:
    snprintf(buf, buf_len, "%s ($%.2x),Y", opname, operand1);
    break;
  case k_ind:
    snprintf(buf, buf_len, "%s ($%.4x)", opname, addr);
    break;
  case k_rel:
    addr = reg_pc + 2 + (char) operand1;
    snprintf(buf, buf_len, "%s $%.4x", opname, addr);
    break;
  default:
    snprintf(buf, buf_len, "%s: %.2x", opname, opcode);
    break;
  }
}

static int
debug_get_addr(unsigned char opcode,
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
    return -1;
    break;
  }
  return addr;
}

static void
debug_print_branch(char* p_buf,
                   size_t buf_len,
                   unsigned char opcode,
                   unsigned char fn_6502,
                   unsigned char fo_6502,
                   unsigned char fc_6502,
                   unsigned char fz_6502) {
  int taken = -1;
  switch (g_optypes[opcode]) {
  case k_bpl:
    taken = !fn_6502;
    break;
  case k_bmi:
    taken = fn_6502;
    break;
  case k_bvc:
    taken = !fo_6502;
    break;
  case k_bvs:
    taken = fo_6502;
    break;
  case k_bcc:
    taken = !fc_6502;
    break;
  case k_bcs:
    taken = fc_6502;
    break;
  case k_bne:
    taken = !fz_6502;
    break;
  case k_beq:
    taken = fz_6502;
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

static void
debug_disass(unsigned char* p_mem, uint16_t addr_6502) {
  size_t i;
  for (i = 0; i < 20; ++i) {
    char opcode_buf[k_max_opcode_len];
    uint16_t addr_plus_1 = addr_6502 + 1;
    uint16_t addr_plus_2 = addr_6502 + 2;
    unsigned char opcode = p_mem[addr_6502];
    unsigned char opmode = g_opmodes[opcode];
    unsigned char oplen = g_opmodelens[opmode];
    unsigned char operand1 = p_mem[addr_plus_1];
    unsigned char operand2 = p_mem[addr_plus_2];
    debug_print_opcode(opcode_buf,
                       sizeof(opcode_buf),
                       opcode,
                       operand1,
                       operand2,
                       addr_6502);
    printf("%.4x: %s\n", addr_6502, opcode_buf);
    addr_6502 += oplen;
  }
}

static int
debug_hit_break(uint16_t ip_6502, int addr_6502, unsigned char opcode_6502) {
  size_t i;
  for (i = 0; i < k_max_break; ++i) {
    if (debug_break_exec[i] == ip_6502) {
      return 1;
    }
    if (addr_6502 != -1 &&
        debug_break_mem_low[i] <= addr_6502 &&
        debug_break_mem_high[i] >= addr_6502) {
      return 1;
    }
  }
  if (debug_break_opcodes[opcode_6502]) {
    return 1;
  }

  return 0;
}

static int
debug_sort_opcodes(const void* p_op1, const void* p_op2, void* p_state) {
  struct debug_struct* p_debug = (struct debug_struct*) p_state;
  unsigned char op1 = *(unsigned char*) p_op1;
  unsigned char op2 = *(unsigned char*) p_op2;
  return p_debug->count_opcode[op1] - p_debug->count_opcode[op2];
}

static int
debug_sort_addrs(const void* p_addr1, const void* p_addr2, void* p_state) {
  struct debug_struct* p_debug = (struct debug_struct*) p_state;
  uint16_t addr1 = *(uint16_t*) p_addr1;
  uint16_t addr2 = *(uint16_t*) p_addr2;
  return p_debug->count_addr[addr1] - p_debug->count_addr[addr2];
}

static void
debug_dump_stats(struct debug_struct* p_debug) {
  size_t i;
  unsigned char sorted_opcodes[k_6502_op_num_opcodes];
  uint16_t sorted_addrs[k_bbc_addr_space_size];

  for (i = 0; i < k_6502_op_num_opcodes; ++i) {
    sorted_opcodes[i] = i;
  }
  qsort_r(sorted_opcodes,
          k_6502_op_num_opcodes,
          sizeof(unsigned char),
          debug_sort_opcodes,
          p_debug);
  printf("=== Opcodes ===\n");
  for (i = 0; i < k_6502_op_num_opcodes; ++i) {
    char opcode_buf[k_max_opcode_len];
    unsigned char opcode = sorted_opcodes[i];
    size_t count = p_debug->count_opcode[opcode];
    if (!count) {
      continue;
    }
    debug_print_opcode(opcode_buf, sizeof(opcode_buf), opcode, 0, 0, 0xfffe);
    printf("%12s: %zu\n", opcode_buf, count);
  }

  for (i = 0; i < k_bbc_addr_space_size; ++i) {
    sorted_addrs[i] = i;
  }
  qsort_r(sorted_addrs,
          k_bbc_addr_space_size,
          sizeof(uint16_t),
          debug_sort_addrs,
          p_debug);
  printf("=== Addrs ===\n");
  for (i = k_bbc_addr_space_size - 256; i < k_bbc_addr_space_size; ++i) {
    uint16_t addr = sorted_addrs[i];
    size_t count = p_debug->count_addr[addr];
    if (!count) {
      continue;
    }
    printf("%4x: %zu\n", addr, count);
  }
}

void
debug_callback(struct debug_struct* p_debug) {
  struct bbc_struct* p_bbc = p_debug->p_bbc;
  unsigned char* p_mem = bbc_get_mem(p_bbc);
  char opcode_buf[k_max_opcode_len];
  char extra_buf[k_max_extra_len];
  char input_buf[k_max_input_len];
  char flags_buf[9];
  /* NOTE: not correct for execution in hardware registers. */
  unsigned char opcode;
  unsigned char operand1;
  unsigned char operand2;
  int addr_6502;
  int hit_break;
  unsigned char reg_a;
  unsigned char reg_x;
  unsigned char reg_y;
  unsigned char reg_s;
  unsigned char reg_flags;
  uint16_t reg_pc;
  uint16_t reg_pc_plus_1;
  uint16_t reg_pc_plus_2;
  unsigned char flag_z;
  unsigned char flag_n;
  unsigned char flag_c;
  unsigned char flag_o;
  int do_trap = 0;

  bbc_get_registers(p_bbc, &reg_a, &reg_x, &reg_y, &reg_s, &reg_flags, &reg_pc);
  flag_z = !!(reg_flags & 0x02);
  flag_n = !!(reg_flags & 0x80);
  flag_c = !!(reg_flags & 0x01);
  flag_o = !!(reg_flags & 0x40);

  opcode = p_mem[reg_pc];
  reg_pc_plus_1 = reg_pc + 1;
  reg_pc_plus_2 = reg_pc + 2;
  operand1 = p_mem[reg_pc_plus_1];
  operand2 = p_mem[reg_pc_plus_2];

  if (!debug_inited) {
    debug_init();
  }

  if (p_debug->stats) {
    unsigned char optype = g_optypes[opcode];
    unsigned char opmode = g_opmodes[opcode];
    p_debug->count_addr[reg_pc]++;
    p_debug->count_opcode[opcode]++;
    p_debug->count_optype[optype]++;
    p_debug->count_opmode[opmode]++;
  }

  addr_6502 = debug_get_addr(opcode, operand1, operand2, reg_x, reg_y, p_mem);

  hit_break = debug_hit_break(reg_pc, addr_6502, opcode);

  if (debug_running && !hit_break && !debug_running_print) {
    return;
  }

  extra_buf[0] = '\0';
  if (addr_6502 != -1) {
    snprintf(extra_buf,
             sizeof(extra_buf),
             "[addr=%.4x val=%.2x]",
             addr_6502,
             p_mem[addr_6502]);
  } else {
    debug_print_branch(extra_buf,
                       sizeof(extra_buf),
                       opcode,
                       flag_n,
                       flag_o,
                       flag_c,
                       flag_z);
  }

  debug_print_opcode(opcode_buf,
                     sizeof(opcode_buf),
                     opcode,
                     operand1,
                     operand2,
                     reg_pc);

  memset(flags_buf, ' ', 8);
  flags_buf[8] = '\0';
  if (flag_c) {
    flags_buf[0] = 'C';
  }
  if (flag_z) {
    flags_buf[1] = 'Z';
  }
  if (reg_flags & 0x04) {
    flags_buf[2] = 'I';
  }
  if (reg_flags & 0x08) {
    flags_buf[3] = 'D';
  }
  flags_buf[5] = '1';
  if (flag_o) {
    flags_buf[6] = 'O';
  }
  if (flag_n) {
    flags_buf[7] = 'N';
  }

  printf("%.4x: %-16s [A=%.2x X=%.2x Y=%.2x S=%.2x F=%s] %s\n",
         reg_pc,
         opcode_buf,
         reg_a,
         reg_x,
         reg_y,
         reg_s,
         flags_buf,
         extra_buf);
  fflush(stdout);

  if (debug_running && !hit_break) {
    return;
  }
  debug_running = 0;

  while (1) {
    char* input_ret;
    size_t i;
    int parse_int = -1;
    int parse_int2 = -1;
    int parse_int3 = -1;
    char parse_string[256];
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

    if (!strcmp(input_buf, "")) {
      memcpy(input_buf, debug_old_input_buf, k_max_input_len);
    } else {
      memcpy(debug_old_input_buf, input_buf, k_max_input_len);
    }

    if (!strcmp(input_buf, "q")) {
      exit(0);
    } else if (!strcmp(input_buf, "p")) {
      debug_running_print = !debug_running_print;
      printf("print now: %d\n", debug_running_print);
    } else if (!strcmp(input_buf, "stats")) {
      p_debug->stats = !p_debug->stats;
      printf("stats now: %d\n", p_debug->stats);
    } else if (!strcmp(input_buf, "ds")) {
      debug_dump_stats(p_debug);
    } else if (!strcmp(input_buf, "s")) {
      break;
    } else if (!strcmp(input_buf, "t")) {
      do_trap = 1;
      break;
    } else if (!strcmp(input_buf, "c")) {
      debug_running = 1;
      break;
    } else if (sscanf(input_buf, "m %x", &parse_int) == 1) {
      parse_addr = parse_int;
      printf("%04x:", parse_addr);
      for (i = 0; i < 16; ++i) {
        printf(" %02x", p_mem[parse_addr]);
        parse_addr++;
      }
      printf("  ");
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
    } else if (sscanf(input_buf, "b %d %x", &parse_int, &parse_int2) == 2 &&
               parse_int >= 0 &&
               parse_int < k_max_break) {
      parse_addr = parse_int2;
      debug_break_exec[parse_int] = parse_addr;
    } else if (sscanf(input_buf,
                      "bm %d %x %x",
                      &parse_int,
                      &parse_int2,
                      &parse_int3) >=2 &&
               parse_int >= 0 &&
               parse_int < k_max_break) {
      parse_addr = parse_int2;
      debug_break_mem_low[parse_int] = parse_addr;
      if (parse_int3 != -1) {
        parse_addr = parse_int3;
      }
      debug_break_mem_high[parse_int] = parse_addr;
    } else if (sscanf(input_buf, "db %d", &parse_int) == 1 &&
               parse_int >= 0 &&
               parse_int < k_max_break) {
      debug_break_exec[parse_int] = -1;
    } else if (sscanf(input_buf, "dbm %d", &parse_int) == 1 &&
               parse_int >= 0 &&
               parse_int < k_max_break) {
      debug_break_mem_low[parse_int] = -1;
      debug_break_mem_high[parse_int] = -1;
    } else if (sscanf(input_buf, "int %d %x", &parse_int, &parse_int2) == 2 &&
               parse_int == 0) {
      bbc_fire_interrupt(p_bbc, parse_int, parse_int2 & 0x7f);
    } else if (sscanf(input_buf, "bop %x", &parse_int) == 1 &&
               parse_int >= 0 &&
               parse_int < 256) {
      debug_break_opcodes[parse_int] = 1;
    } else if (sscanf(input_buf, "sm %x %x", &parse_int, &parse_int2) == 2 &&
               parse_int >= 0 &&
               parse_int < 65536) {
      p_mem[parse_int] = parse_int2;
    } else if (sscanf(input_buf,
                      "lm %255s %x %x",
                      parse_string,
                      &parse_int,
                      &parse_int2) == 3 &&
               parse_int >= 0 &&
               parse_int < 65536 &&
               parse_int2 >= 0 &&
               parse_int2 <= 65536 &&
               parse_int + parse_int2 <= 65536) {
      parse_string[255] = '\0';
      state_load_memory(p_bbc, parse_string, parse_int, parse_int2);
    } else if (sscanf(input_buf, "ss %255s", parse_string) == 1) {
      parse_string[255] = '\0';
      state_save(p_bbc, parse_string);
    } else if (sscanf(input_buf, "a=%x", &parse_int) == 1) {
      reg_a = parse_int;
    } else if (sscanf(input_buf, "x=%x", &parse_int) == 1) {
      reg_x = parse_int;
    } else if (sscanf(input_buf, "y=%x", &parse_int) == 1) {
      reg_y = parse_int;
    } else if (sscanf(input_buf, "s=%x", &parse_int) == 1) {
      reg_s = parse_int;
    } else if (sscanf(input_buf, "d %x", &parse_int) == 1) {
      debug_disass(p_mem, parse_int);
    } else if (!strcmp(input_buf, "?")) {
      printf("q                : quit\n");
      printf("c                : continue\n");
      printf("s                : step one 6502 instuction\n");
      printf("d <addr>         : disassemble at <addr>\n");
      printf("t                : trap into gdb\n");
      printf("b <id> <addr>    : set breakpoint <id> at 6502 address <addr>\n");
      printf("db <id>          : delete breakpoint <id>\n");
      printf("bm <id> <lo> (hi): set memory breakpoint for 6502 range\n");
      printf("dbm <id>         : delete memory breakpoint <id>\n");
      printf("bop <op>         : break on opcode <op>\n");
      printf("m <addr>         : show memory at <addr>\n");
      printf("sm <addr> <val>  : write <val> to 6502 <addr>\n");
      printf("lm <f> <addr> <l>: load <l> memory at <addr> from state <f>\n");
      printf("ss <f>           : save state to BEM file <f>\n");
      printf("{a,x,y}=<val>    : set register to <val>\n");
    } else {
      printf("???\n");
    }
    bbc_set_registers(p_bbc, reg_a, reg_x, reg_y, reg_s, reg_flags, reg_pc);
    fflush(stdout);
  }
  if (do_trap) {
    int ret = raise(SIGTRAP);
    if (ret != 0) {
      errx(1, "raise failed");
    }
  }
}
