#define _GNU_SOURCE /* For qsort_r() */

#include "debug.h"

#include "bbc.h"
/* TODO: get rid of. */
#include "jit.h"
#include "defs_6502.h"
#include "state.h"
#include "util.h"
#include "via.h"

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*sighandler_t)(int);

static const size_t k_max_opcode_len = 12 + 1;
static const size_t k_max_extra_len = 32;
enum {
  k_max_break = 16,
};
enum {
  k_max_input_len = 256,
};

struct debug_struct {
  struct bbc_struct* p_bbc;
  int debug_active;
  int debug_running;
  int debug_running_print;
  int debug_running_slow;
  /* Breakpointing. */
  uint16_t debug_stop_addr;
  uint16_t debug_counter_addr;
  uint16_t next_or_finish_stop_addr;
  int debug_break_exec[k_max_break];
  int debug_break_mem_low[k_max_break];
  int debug_break_mem_high[k_max_break];
  int debug_break_opcodes[256];
  /* Stats. */
  int stats;
  size_t count_addr[k_6502_addr_space_size];
  size_t count_opcode[k_6502_op_num_opcodes];
  size_t count_optype[k_6502_op_num_types];
  size_t count_opmode[k_6502_op_num_modes];
  size_t rom_write_faults;
  /* Other. */
  uint64_t time_basis;
  size_t next_cycles;
  unsigned char warned_at_addr[k_6502_addr_space_size];
  size_t counter;
  char debug_old_input_buf[k_max_input_len];
};

static int s_sigint_received;

static void
sigint_handler(int signum) {
  (void) signum;
  s_sigint_received = 1;
}

struct debug_struct*
debug_create(struct bbc_struct* p_bbc,
             int debug_active,
             uint16_t debug_stop_addr) {
  size_t i;
  sighandler_t ret_sig;

  struct debug_struct* p_debug = malloc(sizeof(struct debug_struct));
  if (p_debug == NULL) {
    errx(1, "couldn't allocate debug_struct");
  }
  (void) memset(p_debug, '\0', sizeof(struct debug_struct));

  ret_sig = signal(SIGINT, sigint_handler);
  if (ret_sig == SIG_ERR) {
    errx(1, "signal failed");
  }

  p_debug->p_bbc = p_bbc;
  p_debug->debug_active = debug_active;
  p_debug->debug_running = bbc_get_run_flag(p_bbc);
  p_debug->debug_running_print = bbc_get_print_flag(p_bbc);
  p_debug->debug_running_slow = bbc_get_slow_flag(p_bbc);
  p_debug->debug_stop_addr = debug_stop_addr;
  p_debug->time_basis = util_gettime();
  p_debug->next_cycles = 0;

  for (i = 0; i < k_max_break; ++i) {
    p_debug->debug_break_exec[i] = -1;
    p_debug->debug_break_mem_low[i] = -1;
    p_debug->debug_break_mem_high[i] = -1;
  }

  return p_debug;
}

void
debug_destroy(struct debug_struct* p_debug) {
  free(p_debug);
}

int
debug_subsystem_active(void* p) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  if (p_debug->debug_active ||
      p_debug->debug_stop_addr ||
      p_debug->debug_counter_addr) {
    return 1;
  }
  return 0;
}

int
debug_active_at_addr(void* p, uint16_t addr_6502) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  if (p_debug->debug_active || (addr_6502 == p_debug->debug_stop_addr)) {
    return 1;
  }
  return 0;
}

int
debug_counter_at_addr(void* p, uint16_t addr_6502) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  if (addr_6502 == p_debug->debug_counter_addr) {
    return 1;
  }
  return 0;
}

size_t*
debug_get_counter_ptr(void* p) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  return &p_debug->counter;
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
  case k_acc:
    snprintf(buf, buf_len, "%s A", opname);
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
debug_get_addr(int* wrapped,
               unsigned char opcode,
               unsigned char operand1,
               unsigned char operand2,
               unsigned char x_6502,
               unsigned char y_6502,
               unsigned char* p_mem) {
  unsigned char opmode = g_opmodes[opcode];
  unsigned int addr;
  uint16_t trunc_addr;
  uint16_t addr_addr;

  *wrapped = 0;

  switch (opmode) {
  case k_zpg:
    addr = operand1;
    trunc_addr = addr;
    break;
  case k_zpx:
    addr = (operand1 + x_6502);
    trunc_addr = (unsigned char) addr;
    break;
  case k_zpy:
    addr = (unsigned char) (operand1 + y_6502);
    trunc_addr = (unsigned char) addr;
    break;
  case k_abs:
    addr = (uint16_t) (operand1 + (operand2 << 8));
    trunc_addr = addr;
    break;
  case k_abx:
    addr = (operand1 + (operand2 << 8) + x_6502);
    trunc_addr = (uint16_t) addr;
    break;
  case k_aby:
    addr = (operand1 + (operand2 << 8) + y_6502);
    trunc_addr = (uint16_t) addr;
    break;
  case k_idx:
    addr_addr = (operand1 + x_6502);
    trunc_addr = (unsigned char) addr_addr;
    if (trunc_addr != addr_addr || addr_addr == 0xff) {
      *wrapped = 1;
    }
    addr = p_mem[(unsigned char) (addr_addr + 1)];
    addr <<= 8;
    addr |= p_mem[(unsigned char) addr_addr];
    trunc_addr = addr;
    break;
  case k_idy:
    if (operand1 == 0xff) {
      *wrapped = 1;
    }
    addr = p_mem[(unsigned char) (operand1 + 1)];
    addr <<= 8;
    addr |= p_mem[operand1];
    addr = (addr + y_6502);
    trunc_addr = (uint16_t) addr;
    break;
  default:
    return -1;
    break;
  }

  if (trunc_addr != addr) {
    *wrapped = 1;
  }
  return trunc_addr;
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
debug_disass(struct bbc_struct* p_bbc, uint16_t addr_6502) {
  size_t i;
  unsigned char* p_mem = bbc_get_mem(p_bbc);

  for (i = 0; i < 20; ++i) {
    char opcode_buf[k_max_opcode_len];

    uint16_t addr_plus_1 = addr_6502 + 1;
    uint16_t addr_plus_2 = addr_6502 + 2;
    unsigned char opcode = p_mem[addr_6502];
    unsigned char opmode = g_opmodes[opcode];
    unsigned char oplen = g_opmodelens[opmode];
    unsigned char operand1 = p_mem[addr_plus_1];
    unsigned char operand2 = p_mem[addr_plus_2];
    uint16_t block_6502 = bbc_get_block(p_bbc, addr_6502);
    debug_print_opcode(opcode_buf,
                       sizeof(opcode_buf),
                       opcode,
                       operand1,
                       operand2,
                       addr_6502);
    printf("[%.4x] %.4x: %s\n", block_6502, addr_6502, opcode_buf);
    addr_6502 += oplen;
  }
}

static void
debug_dump_via(struct bbc_struct* p_bbc, int id) {
  struct via_struct* p_via;
  unsigned char ORA;
  unsigned char ORB;
  unsigned char DDRA;
  unsigned char DDRB;
  unsigned char SR;
  unsigned char ACR;
  unsigned char PCR;
  unsigned char IFR;
  unsigned char IER;
  unsigned char peripheral_a;
  unsigned char peripheral_b;
  int T1C;
  int T1L;
  int T2C;
  int T2L;
  unsigned char t1_oneshot_fired;
  unsigned char t2_oneshot_fired;

  if (id == k_via_system) {
    printf("System VIA\n");
    p_via = bbc_get_sysvia(p_bbc);
  } else if (id == k_via_user) {
    printf("User VIA\n");
    p_via = bbc_get_uservia(p_bbc);
  } else {
    assert(0);
  }
  via_get_registers(p_via,
                    &ORA,
                    &ORB,
                    &DDRA,
                    &DDRB,
                    &SR,
                    &ACR,
                    &PCR,
                    &IFR,
                    &IER,
                    &peripheral_a,
                    &peripheral_b,
                    &T1C,
                    &T1L,
                    &T2C,
                    &T2L,
                    &t1_oneshot_fired,
                    &t2_oneshot_fired);
  printf("IFR %.2x IER %.2x\n", IFR, IER);
  printf("ORA %.2x DDRA %.2x periph %.2x\n", ORA, DDRA, peripheral_a);
  printf("ORB %.2x DDRB %.2x periph %.2x\n", ORB, DDRB, peripheral_b);
  printf("SR %.2x ACR %.2x PCR %.2x\n", SR, ACR, PCR);
  printf("T1L %.4x T1C %.4x oneshot hit %d\n", T1L, T1C, t1_oneshot_fired);
  printf("T2L %.4x T2C %.4x oneshot hit %d\n", T2L, T2C, t2_oneshot_fired);
}

static int
debug_hit_break(struct debug_struct* p_debug,
                uint16_t reg_pc,
                int addr_6502,
                unsigned char opcode_6502) {
  size_t i;
  for (i = 0; i < k_max_break; ++i) {
    if (reg_pc == p_debug->debug_break_exec[i]) {
      return 1;
    }
    if (addr_6502 != -1 &&
        p_debug->debug_break_mem_low[i] <= addr_6502 &&
        p_debug->debug_break_mem_high[i] >= addr_6502) {
      return 1;
    }
  }
  if (p_debug->debug_break_opcodes[opcode_6502]) {
    return 1;
  }
  if (reg_pc == p_debug->next_or_finish_stop_addr) {
    return 1;
  }
  if (reg_pc == p_debug->debug_stop_addr) {
    return 1;
  }

  return 0;
}

static int
debug_sort_opcodes(const void* p_op1, const void* p_op2, void* p_state) {
  struct debug_struct* p_debug = (struct debug_struct*) p_state;
  unsigned char op1 = *(unsigned char*) p_op1;
  unsigned char op2 = *(unsigned char*) p_op2;
  return (p_debug->count_opcode[op1] - p_debug->count_opcode[op2]);
}

static int
debug_sort_addrs(const void* p_addr1, const void* p_addr2, void* p_state) {
  struct debug_struct* p_debug = (struct debug_struct*) p_state;
  uint16_t addr1 = *(uint16_t*) p_addr1;
  uint16_t addr2 = *(uint16_t*) p_addr2;
  return (p_debug->count_addr[addr1] - p_debug->count_addr[addr2]);
}

static void
debug_dump_stats(struct debug_struct* p_debug) {
  size_t i;
  unsigned char sorted_opcodes[k_6502_op_num_opcodes];
  uint16_t sorted_addrs[k_6502_addr_space_size];

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
    printf("%14s: %zu\n", opcode_buf, count);
  }

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    sorted_addrs[i] = i;
  }
  qsort_r(sorted_addrs,
          k_6502_addr_space_size,
          sizeof(uint16_t),
          debug_sort_addrs,
          p_debug);
  printf("=== Addrs ===\n");
  for (i = k_6502_addr_space_size - 256; i < k_6502_addr_space_size; ++i) {
    uint16_t addr = sorted_addrs[i];
    size_t count = p_debug->count_addr[addr];
    if (!count) {
      continue;
    }
    printf("%4x: %zu\n", addr, count);
  }
  printf("--> rom_write_faults: %zu\n", p_debug->rom_write_faults);
}

static void
debug_check_unusual(struct debug_struct* p_debug,
                    unsigned char opcode,
                    uint16_t reg_pc,
                    uint16_t addr_6502,
                    int wrapped) {
  int is_write;
  int is_register;
  int is_rom;
  int has_code;

  struct bbc_struct* p_bbc = p_debug->p_bbc;
  struct jit_struct* p_jit = bbc_get_jit(p_bbc);
  unsigned char opmode = g_opmodes[opcode];
  unsigned char optype = g_optypes[opcode];
  unsigned char opmem = g_opmem[optype];

  is_write = ((opmem == k_write || opmem == k_rw) &&
              opmode != k_nil &&
              opmode != k_acc);
  is_register = (addr_6502 >= k_bbc_registers_start &&
                 addr_6502 < k_bbc_registers_start + k_bbc_registers_len);
  is_rom = (!is_register && addr_6502 >= k_bbc_ram_size);
  has_code = jit_has_code(p_jit, addr_6502);

  /* Currently unimplemented and untrapped: indirect reads into the hardware
   * register space.
   */
  if (is_register && (opmode == k_idx || opmode == k_idy)) {
    printf("Indirect access to register %.4x at %.4x\n", addr_6502, reg_pc);
    p_debug->debug_running = 0;
  }

  /* Handled via various means (sometimes SIGSEGV handler!) but worth noting. */
  if (is_write && is_rom) {
    if (!p_debug->warned_at_addr[reg_pc]) {
      printf("Code at %.4x is writing to ROM at %.4x\n", reg_pc, addr_6502);
      p_debug->warned_at_addr[reg_pc] = 1;
    }
    if (p_debug->stats && (opmode == k_idx || opmode == k_idy)) {
      p_debug->rom_write_faults++;
    }
  }

  /* Currently unimplemented and untrapped.
   * NOTE: this is now implmented but I've never seen a game use it, so keeping
   * it in so we can find such a game and write some notes about it.
   */
  if (is_write && has_code) {
    if (!p_debug->warned_at_addr[reg_pc]) {
      printf("Code at %.4x modifying code at %.4x\n", reg_pc, addr_6502);
      p_debug->warned_at_addr[reg_pc] = 1;
    }
    if (addr_6502 < 0x3000 && (opmode == k_idx || opmode == k_idy)) {
      printf("Indirect write at %.4x to %.4x\n", reg_pc, addr_6502);
      p_debug->debug_running = 0;
    }
  }

  /* Look for zero page wrap or full address space wraps. We want to prove
   * these are unusual to move to more fault-based fixups.
   */
  if (wrapped) {
    printf("ADDRESS WRAP AROUND at %.4x to %.4x\n", reg_pc, addr_6502);
    /*debug_running = 0;*/
  }
}

static void
debug_slow_down(struct debug_struct* p_debug) {
  struct bbc_struct* p_bbc = p_debug->p_bbc;
  size_t cycles = bbc_get_cycles(p_bbc);

  if (cycles >= p_debug->next_cycles) {
    util_sleep_until(p_debug->time_basis + (cycles / 2));
    p_debug->next_cycles += 2000;
  }
}

static void
debug_load_raw(struct debug_struct* p_debug,
               const char* p_file_name,
               uint16_t addr_6502) {
  size_t len;
  unsigned char buf[k_6502_addr_space_size];

  struct bbc_struct* p_bbc = p_debug->p_bbc;

  len = util_read_file(buf, sizeof(buf), p_file_name);

  bbc_set_memory_block(p_bbc, addr_6502, len, buf);
}

void*
debug_callback(void* p) {
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
  uint16_t block_6502;
  unsigned char flag_z;
  unsigned char flag_n;
  unsigned char flag_c;
  unsigned char flag_o;
  int wrapped;
  unsigned char opmode;
  size_t oplen;

  struct debug_struct* p_debug = (struct debug_struct*) p;
  struct bbc_struct* p_bbc = p_debug->p_bbc;
  unsigned char* p_mem = bbc_get_mem(p_bbc);
  int do_trap = 0;
  void* ret_intel_pc = 0;
  volatile int* p_sigint_received = &s_sigint_received;

  bbc_get_registers(p_bbc, &reg_a, &reg_x, &reg_y, &reg_s, &reg_flags, &reg_pc);
  flag_z = !!(reg_flags & 0x02);
  flag_n = !!(reg_flags & 0x80);
  flag_c = !!(reg_flags & 0x01);
  flag_o = !!(reg_flags & 0x40);

  opcode = p_mem[reg_pc];
  opmode = g_opmodes[opcode];
  oplen = g_opmodelens[opmode];
  reg_pc_plus_1 = reg_pc + 1;
  reg_pc_plus_2 = reg_pc + 2;
  operand1 = p_mem[reg_pc_plus_1];
  operand2 = p_mem[reg_pc_plus_2];

  if (p_debug->stats) {
    unsigned char optype = g_optypes[opcode];
    p_debug->count_addr[reg_pc]++;
    p_debug->count_opcode[opcode]++;
    p_debug->count_optype[optype]++;
    p_debug->count_opmode[opmode]++;
  }

  addr_6502 = debug_get_addr(&wrapped,
                             opcode,
                             operand1,
                             operand2,
                             reg_x,
                             reg_y,
                             p_mem);

  debug_check_unusual(p_debug, opcode, reg_pc, addr_6502, wrapped);

  if (p_debug->debug_running_slow) {
    debug_slow_down(p_debug);
  }

  hit_break = debug_hit_break(p_debug, reg_pc, addr_6502, opcode);

  if (*p_sigint_received) {
    *p_sigint_received = 0;
    p_debug->debug_running = 0;
  }

  if (p_debug->debug_running && !hit_break && !p_debug->debug_running_print) {
    return 0;
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

  (void) memset(flags_buf, ' ', 8);
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

  block_6502 = bbc_get_block(p_bbc, reg_pc);

  printf("[%.4x] %.4x: %-14s [A=%.2x X=%.2x Y=%.2x S=%.2x F=%s] %s\n",
         block_6502,
         reg_pc,
         opcode_buf,
         reg_a,
         reg_x,
         reg_y,
         reg_s,
         flags_buf,
         extra_buf);
  fflush(stdout);

  if (p_debug->debug_running && !hit_break) {
    return 0;
  }
  p_debug->debug_running = 0;
  if (reg_pc == p_debug->next_or_finish_stop_addr) {
    p_debug->next_or_finish_stop_addr = 0;
  }

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
      (void) memcpy(input_buf, p_debug->debug_old_input_buf, k_max_input_len);
    } else {
      (void) memcpy(p_debug->debug_old_input_buf, input_buf, k_max_input_len);
    }

    if (!strcmp(input_buf, "q")) {
      exit(0);
    } else if (!strcmp(input_buf, "p")) {
      p_debug->debug_running_print = !p_debug->debug_running_print;
      printf("print now: %d\n", p_debug->debug_running_print);
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
      p_debug->debug_running = 1;
      break;
    } else if (!strcmp(input_buf, "n")) {
      p_debug->next_or_finish_stop_addr = reg_pc + oplen;
      p_debug->debug_running = 1;
      break;
    } else if (!strcmp(input_buf, "f")) {
      uint16_t finish_addr;
      unsigned char stack = reg_s + 1;
      finish_addr = p_mem[k_6502_stack_addr + stack];
      stack++;
      finish_addr |= (p_mem[k_6502_stack_addr + stack] << 8);
      finish_addr++;
      p_debug->next_or_finish_stop_addr = finish_addr;
      p_debug->debug_running = 1;
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
      p_debug->debug_break_exec[parse_int] = parse_addr;
    } else if (sscanf(input_buf,
                      "bm %d %x %x",
                      &parse_int,
                      &parse_int2,
                      &parse_int3) >=2 &&
               parse_int >= 0 &&
               parse_int < k_max_break) {
      parse_addr = parse_int2;
      p_debug->debug_break_mem_low[parse_int] = parse_addr;
      if (parse_int3 != -1) {
        parse_addr = parse_int3;
      }
      p_debug->debug_break_mem_high[parse_int] = parse_addr;
    } else if (sscanf(input_buf, "db %d", &parse_int) == 1 &&
               parse_int >= 0 &&
               parse_int < k_max_break) {
      p_debug->debug_break_exec[parse_int] = -1;
    } else if (sscanf(input_buf, "dbm %d", &parse_int) == 1 &&
               parse_int >= 0 &&
               parse_int < k_max_break) {
      p_debug->debug_break_mem_low[parse_int] = -1;
      p_debug->debug_break_mem_high[parse_int] = -1;
    } else if (sscanf(input_buf, "bop %x", &parse_int) == 1 &&
               parse_int >= 0 &&
               parse_int < 256) {
      p_debug->debug_break_opcodes[parse_int] = 1;
    } else if (sscanf(input_buf, "sm %x %x", &parse_int, &parse_int2) == 2 &&
               parse_int >= 0 &&
               parse_int < 65536) {
      bbc_memory_write(p_bbc, parse_int, parse_int2);
    } else if (sscanf(input_buf, "inv %x", &parse_int) == 1 &&
               parse_int >= 0 &&
               parse_int < 65536) {
      bbc_memory_write(p_bbc, parse_int, p_mem[parse_int]);
    } else if (sscanf(input_buf, "stopat %x", &parse_int) == 1 &&
               parse_int >= 0 &&
               parse_int < 65536) {
      p_debug->debug_stop_addr = parse_int;
    } else if (sscanf(input_buf, "counterat %x", &parse_int) == 1 &&
               parse_int >= 0 &&
               parse_int < 65536) {
      p_debug->debug_counter_addr = parse_int;
    } else if (sscanf(input_buf, "counter %d", &parse_int) == 1 &&
               parse_int > 0) {
      p_debug->counter = parse_int;
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
    } else if (sscanf(input_buf,
                      "lr %255s %x",
                      parse_string,
                      &parse_int) == 2 &&
               parse_int >= 0 &&
               parse_int < 65536) {
      parse_string[255] = '\0';
      debug_load_raw(p_debug, parse_string, parse_int);
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
    } else if (sscanf(input_buf, "pc=%x", &parse_int) == 1) {
      reg_pc = parse_int;
      ret_intel_pc = jit_get_jit_base_addr(bbc_get_jit(p_bbc), reg_pc);
    } else if (sscanf(input_buf, "d %x", &parse_int) == 1) {
      debug_disass(p_bbc, parse_int);
    } else if (!strcmp(input_buf, "d")) {
      debug_disass(p_bbc, reg_pc);
    } else if (!strcmp(input_buf, "sys")) {
      debug_dump_via(p_bbc, k_via_system);
    } else if (!strcmp(input_buf, "user")) {
      debug_dump_via(p_bbc, k_via_user);
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
      printf("lr <f> <addr>    : load memory at <addr> from raw file <f>\n");
      printf("ss <f>           : save state to BEM file <f>\n");
      printf("{a,x,y,pc}=<val> : set register to <val>\n");
      printf("sys              : show system VIA registers\n");
      printf("user             : show user VIA registers\n");
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
  return ret_intel_pc;
}
