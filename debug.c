#define _GNU_SOURCE /* For qsort_r() */

#include "debug.h"

#include "bbc.h"
/* TODO: get rid of. */
#include "jit.h"
#include "defs_6502.h"
#include "state.h"
#include "state_6502.h"
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
  /* Breakpointing. */
  uint16_t debug_stop_addr;
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
  uint8_t warn_at_addr_count[k_6502_addr_space_size];
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
  p_debug->debug_stop_addr = debug_stop_addr;
  p_debug->time_basis = util_gettime_us();
  p_debug->next_cycles = 0;

  for (i = 0; i < k_max_break; ++i) {
    p_debug->debug_break_exec[i] = -1;
    p_debug->debug_break_mem_low[i] = -1;
    p_debug->debug_break_mem_high[i] = -1;
  }

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    p_debug->warn_at_addr_count[i] = 1;
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
  if (p_debug->debug_active || p_debug->debug_stop_addr) {
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

static void
debug_print_opcode(char* buf,
                   size_t buf_len,
                   uint8_t opcode,
                   uint8_t operand1,
                   uint8_t operand2,
                   uint16_t reg_pc,
                   int do_irq,
                   struct state_6502* p_state_6502) {
  uint8_t opmode;
  const char* opname;
  uint16_t addr;

  if (do_irq) {
    /* Very close approximation. It's possible a non-NMI IRQ will be reported
     * but then an NMI occurs if the NMI is raised within the first few cycles
     * of the IRQ BRK.
     */
    if (state_6502_check_irq_firing(p_state_6502, k_state_6502_irq_nmi)) {
      (void) snprintf(buf, buf_len, "IRQ (NMI)");
    } else {
      (void) snprintf(buf, buf_len, "IRQ (IRQ)");
    }
    return;
  }

  opmode = g_opmodes[opcode];
  opname = g_p_opnames[g_optypes[opcode]];
  addr = (operand1 | (operand2 << 8));

  switch (opmode) {
  case k_nil:
    (void) snprintf(buf, buf_len, "%s", opname);
    break;
  case k_acc:
    (void) snprintf(buf, buf_len, "%s A", opname);
    break;
  case k_imm:
    (void) snprintf(buf, buf_len, "%s #$%.2X", opname, operand1);
    break;
  case k_zpg:
    (void) snprintf(buf, buf_len, "%s $%.2X", opname, operand1);
    break;
  case k_abs:
    (void) snprintf(buf, buf_len, "%s $%.4X", opname, addr);
    break;
  case k_zpx:
    (void) snprintf(buf, buf_len, "%s $%.2X,X", opname, operand1);
    break;
  case k_zpy:
    (void) snprintf(buf, buf_len, "%s $%.2X,Y", opname, operand1);
    break;
  case k_abx:
    (void) snprintf(buf, buf_len, "%s $%.4X,X", opname, addr);
    break;
  case k_aby:
    (void) snprintf(buf, buf_len, "%s $%.4X,Y", opname, addr);
    break;
  case k_idx:
    (void) snprintf(buf, buf_len, "%s ($%.2X,X)", opname, operand1);
    break;
  case k_idy:
    (void) snprintf(buf, buf_len, "%s ($%.2X),Y", opname, operand1);
    break;
  case k_ind:
    (void) snprintf(buf, buf_len, "%s ($%.4X)", opname, addr);
    break;
  case k_rel:
    addr = (reg_pc + 2 + (char) operand1);
    (void) snprintf(buf, buf_len, "%s $%.4X", opname, addr);
    break;
  default:
    (void) snprintf(buf, buf_len, "%s: %.2x", opname, opcode);
    break;
  }
}

static int
debug_get_addr(int* wrapped_8bit,
               int* wrapped_16bit,
               uint8_t opcode,
               uint8_t operand1,
               uint8_t operand2,
               uint8_t x_6502,
               uint8_t y_6502,
               uint8_t* p_mem) {
  uint8_t opmode = g_opmodes[opcode];
  unsigned int addr;
  uint16_t trunc_addr;
  uint16_t addr_addr;

  int check_wrap_8bit = 1;

  *wrapped_8bit = 0;
  *wrapped_16bit = 0;

  switch (opmode) {
  case k_zpg:
    addr = operand1;
    trunc_addr = addr;
    break;
  case k_zpx:
    addr = (operand1 + x_6502);
    trunc_addr = (uint8_t) addr;
    break;
  case k_zpy:
    addr = (uint8_t) (operand1 + y_6502);
    trunc_addr = (uint8_t) addr;
    break;
  case k_abs:
    addr = (uint16_t) (operand1 + (operand2 << 8));
    check_wrap_8bit = 0;
    trunc_addr = addr;
    break;
  case k_abx:
    addr = (operand1 + (operand2 << 8) + x_6502);
    check_wrap_8bit = 0;
    trunc_addr = (uint16_t) addr;
    break;
  case k_aby:
    addr = (operand1 + (operand2 << 8) + y_6502);
    check_wrap_8bit = 0;
    trunc_addr = (uint16_t) addr;
    break;
  case k_idx:
    addr_addr = (operand1 + x_6502);
    trunc_addr = (uint8_t) addr_addr;
    if ((trunc_addr != addr_addr) || (addr_addr == 0xFF)) {
      *wrapped_8bit = 1;
    }
    addr = p_mem[(uint8_t) (addr_addr + 1)];
    addr <<= 8;
    addr |= p_mem[(uint8_t) addr_addr];
    check_wrap_8bit = 0;
    trunc_addr = addr;
    break;
  case k_idy:
    if (operand1 == 0xFF) {
      *wrapped_8bit = 1;
    }
    addr = p_mem[(uint8_t) (operand1 + 1)];
    addr <<= 8;
    addr |= p_mem[operand1];
    addr = (addr + y_6502);
    check_wrap_8bit = 0;
    trunc_addr = (uint16_t) addr;
    break;
  default:
    return -1;
    break;
  }

  if (trunc_addr != addr) {
    if (check_wrap_8bit) {
      *wrapped_8bit = 1;
    } else {
      *wrapped_16bit = 1;
    }
  }
  return trunc_addr;
}

static void
debug_print_branch(char* p_buf,
                   size_t buf_len,
                   uint8_t opcode,
                   uint8_t fn_6502,
                   uint8_t fo_6502,
                   uint8_t fc_6502,
                   uint8_t fz_6502) {
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
    (void) snprintf(p_buf, buf_len, "[not taken]");
  } else if (taken == 1) {
    (void) snprintf(p_buf, buf_len, "[taken]");
  }
}

static void
debug_disass(struct bbc_struct* p_bbc, uint16_t addr_6502) {
  size_t i;
  uint8_t* p_mem_read = bbc_get_mem_read(p_bbc);

  for (i = 0; i < 20; ++i) {
    char opcode_buf[k_max_opcode_len];

    uint16_t addr_plus_1 = addr_6502 + 1;
    uint16_t addr_plus_2 = addr_6502 + 2;
    uint8_t opcode = p_mem_read[addr_6502];
    uint8_t opmode = g_opmodes[opcode];
    uint8_t oplen = g_opmodelens[opmode];
    uint8_t operand1 = p_mem_read[addr_plus_1];
    uint8_t operand2 = p_mem_read[addr_plus_2];
    uint16_t block_6502 = bbc_get_block(p_bbc, addr_6502);
    debug_print_opcode(opcode_buf,
                       sizeof(opcode_buf),
                       opcode,
                       operand1,
                       operand2,
                       addr_6502,
                       0,
                       NULL);
    (void) printf("[%.4X] %.4X: %s\n", block_6502, addr_6502, opcode_buf);
    addr_6502 += oplen;
  }
}

static void
debug_dump_via(struct bbc_struct* p_bbc, int id) {
  struct via_struct* p_via;
  uint8_t ORA;
  uint8_t ORB;
  uint8_t DDRA;
  uint8_t DDRB;
  uint8_t SR;
  uint8_t ACR;
  uint8_t PCR;
  uint8_t IFR;
  uint8_t IER;
  uint8_t peripheral_a;
  uint8_t peripheral_b;
  int32_t T1C_raw;
  int32_t T1L;
  int32_t T2C_raw;
  int32_t T2L;
  uint8_t t1_oneshot_fired;
  uint8_t t2_oneshot_fired;
  uint8_t t1_pb7;

  if (id == k_via_system) {
    (void) printf("System VIA\n");
    p_via = bbc_get_sysvia(p_bbc);
  } else if (id == k_via_user) {
    (void) printf("User VIA\n");
    p_via = bbc_get_uservia(p_bbc);
  } else {
    assert(0);
    p_via = NULL;
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
                    &T1C_raw,
                    &T1L,
                    &T2C_raw,
                    &T2L,
                    &t1_oneshot_fired,
                    &t2_oneshot_fired,
                    &t1_pb7);
  (void) printf("IFR %.2X IER %.2X\n", IFR, IER);
  (void) printf("ORA %.2X DDRA %.2X periph %.2X\n", ORA, DDRA, peripheral_a);
  (void) printf("ORB %.2X DDRB %.2X periph %.2X\n", ORB, DDRB, peripheral_b);
  (void) printf("SR %.2X ACR %.2X PCR %.2X\n", SR, ACR, PCR);
  (void) printf("T1L %.4X T1C %.4X oneshot hit %d PB7 %d\n",
                T1L,
                (uint16_t) (T1C_raw >> 1),
                t1_oneshot_fired,
                t1_pb7);
  (void) printf("T2L %.4X T2C %.4X oneshot hit %d\n",
                T2L,
                (uint16_t) (T2C_raw >> 1),
                t2_oneshot_fired);
}

static int
debug_hit_break(struct debug_struct* p_debug,
                uint16_t reg_pc,
                int addr_6502,
                uint8_t opcode_6502) {
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
  uint8_t op1 = *(uint8_t*) p_op1;
  uint8_t op2 = *(uint8_t*) p_op2;
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
  uint8_t sorted_opcodes[k_6502_op_num_opcodes];
  uint16_t sorted_addrs[k_6502_addr_space_size];

  for (i = 0; i < k_6502_op_num_opcodes; ++i) {
    sorted_opcodes[i] = i;
  }
  qsort_r(sorted_opcodes,
          k_6502_op_num_opcodes,
          sizeof(uint8_t),
          debug_sort_opcodes,
          p_debug);
  (void) printf("=== Opcodes ===\n");
  for (i = 0; i < k_6502_op_num_opcodes; ++i) {
    char opcode_buf[k_max_opcode_len];
    uint8_t opcode = sorted_opcodes[i];
    size_t count = p_debug->count_opcode[opcode];
    if (!count) {
      continue;
    }
    debug_print_opcode(opcode_buf,
                       sizeof(opcode_buf),
                       opcode,
                       0,
                       0,
                       0xFFFE,
                       0,
                       NULL);
    (void) printf("%14s: %zu\n", opcode_buf, count);
  }

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    sorted_addrs[i] = i;
  }
  qsort_r(sorted_addrs,
          k_6502_addr_space_size,
          sizeof(uint16_t),
          debug_sort_addrs,
          p_debug);
  (void) printf("=== Addrs ===\n");
  for (i = k_6502_addr_space_size - 256; i < k_6502_addr_space_size; ++i) {
    uint16_t addr = sorted_addrs[i];
    size_t count = p_debug->count_addr[addr];
    if (!count) {
      continue;
    }
    (void) printf("%4X: %zu\n", addr, count);
  }
  (void) printf("--> rom_write_faults: %zu\n", p_debug->rom_write_faults);
}

static void
debug_check_unusual(struct debug_struct* p_debug,
                    uint8_t opcode,
                    uint16_t reg_pc,
                    uint16_t addr_6502,
                    int wrapped_8bit,
                    int wrapped_16bit) {
  int is_write;
  int is_register;
  int is_rom;
  int has_code;

  struct bbc_struct* p_bbc = p_debug->p_bbc;
  struct jit_struct* p_jit = bbc_get_jit(p_bbc);
  uint8_t opmode = g_opmodes[opcode];
  uint8_t optype = g_optypes[opcode];
  uint8_t opmem = g_opmem[optype];

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
    (void) printf("Indirect access to register %.4X at %.4X\n",
                  addr_6502,
                  reg_pc);
    p_debug->debug_running = 0;
  }

  /* Handled via various means (sometimes SIGSEGV handler!) but worth noting. */
  if (is_write && is_rom) {
    if (p_debug->warn_at_addr_count[reg_pc]) {
      (void) printf("UNUSUAL: Code at %.4X is writing to ROM at %.4X\n",
                    reg_pc,
                    addr_6502);
      p_debug->warn_at_addr_count[reg_pc]--;
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
    if (p_debug->warn_at_addr_count[reg_pc]) {
      (void) printf("Code at %.4X modifying code at %.4X\n",
                    reg_pc,
                    addr_6502);
      p_debug->warn_at_addr_count[reg_pc]--;
    }
    if (addr_6502 < 0x3000 && (opmode == k_idx || opmode == k_idy)) {
      (void) printf("Indirect write at %.4X to %.4X\n", reg_pc, addr_6502);
      p_debug->debug_running = 0;
    }
  }

  /* Look for zero page wrap or full address space wraps. We want to prove
   * these are unusual to move to more fault-based fixups.
   */
  if (wrapped_8bit) {
    if (p_debug->warn_at_addr_count[reg_pc]) {
      (void) printf("UNUSUAL: 8-bit ADDRESS WRAP at %.4X to %.4X\n",
                    reg_pc,
                    addr_6502);
      p_debug->warn_at_addr_count[reg_pc]--;
    }
  }
  if (wrapped_16bit) {
    if (p_debug->warn_at_addr_count[reg_pc]) {
      (void) printf("VERY UNUSUAL: 16-bit ADDRESS WRAP at %.4X to %.4X\n",
                    reg_pc,
                    addr_6502);
      p_debug->warn_at_addr_count[reg_pc]--;
    }
  }
}

static void
debug_load_raw(struct debug_struct* p_debug,
               const char* p_file_name,
               uint16_t addr_6502) {
  size_t len;
  uint8_t buf[k_6502_addr_space_size];

  struct bbc_struct* p_bbc = p_debug->p_bbc;

  len = util_file_read(buf, sizeof(buf), p_file_name);

  bbc_set_memory_block(p_bbc, addr_6502, len, buf);
}

static void
debug_print_registers(uint8_t reg_a,
                      uint8_t reg_x,
                      uint8_t reg_y,
                      uint8_t reg_s,
                      const char* flags_buf,
                      uint16_t reg_pc,
                      uint64_t cycles) {
  (void) printf("[A=%.2X X=%.2X Y=%.2X S=%.2X F=%s PC=%.4X cycles=%zu]\n",
                reg_a,
                reg_x,
                reg_y,
                reg_s,
                flags_buf,
                reg_pc,
                cycles);
}

static void
debug_print_state(uint16_t block_6502,
                  uint16_t reg_pc,
                  const char* opcode_buf,
                  uint8_t reg_a,
                  uint8_t reg_x,
                  uint8_t reg_y,
                  uint8_t reg_s,
                  const char* flags_buf,
                  const char* extra_buf) {
  (void) printf("[%.4X] %.4X: %-14s [A=%.2X X=%.2X Y=%.2X S=%.2X F=%s] %s\n",
                block_6502,
                reg_pc,
                opcode_buf,
                reg_a,
                reg_x,
                reg_y,
                reg_s,
                flags_buf,
                extra_buf);
}

void*
debug_callback(void* p, int do_irq) {
  char opcode_buf[k_max_opcode_len];
  char extra_buf[k_max_extra_len];
  char input_buf[k_max_input_len];
  char flags_buf[9];
  /* NOTE: not correct for execution in hardware registers. */
  uint8_t opcode;
  uint8_t operand1;
  uint8_t operand2;
  int addr_6502;
  int hit_break;
  uint8_t reg_a;
  uint8_t reg_x;
  uint8_t reg_y;
  uint8_t reg_s;
  uint8_t reg_flags;
  uint16_t reg_pc;
  uint16_t reg_pc_plus_1;
  uint16_t reg_pc_plus_2;
  uint16_t block_6502;
  uint8_t flag_z;
  uint8_t flag_n;
  uint8_t flag_c;
  uint8_t flag_o;
  uint8_t flag_i;
  uint8_t flag_d;
  uint64_t cycles;
  int wrapped_8bit;
  int wrapped_16bit;
  uint8_t opmode;
  size_t oplen;

  struct debug_struct* p_debug = (struct debug_struct*) p;
  struct bbc_struct* p_bbc = p_debug->p_bbc;
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);
  uint8_t* p_mem_read = bbc_get_mem_read(p_bbc);
  int do_trap = 0;
  void* ret_intel_pc = 0;
  volatile int* p_sigint_received = &s_sigint_received;

  bbc_get_registers(p_bbc, &reg_a, &reg_x, &reg_y, &reg_s, &reg_flags, &reg_pc);
  flag_z = !!(reg_flags & 0x02);
  flag_n = !!(reg_flags & 0x80);
  flag_c = !!(reg_flags & 0x01);
  flag_o = !!(reg_flags & 0x40);

  opcode = p_mem_read[reg_pc];
  if (do_irq) {
    opcode = 0;
  }
  opmode = g_opmodes[opcode];
  oplen = g_opmodelens[opmode];
  reg_pc_plus_1 = (reg_pc + 1);
  reg_pc_plus_2 = (reg_pc + 2);
  operand1 = p_mem_read[reg_pc_plus_1];
  operand2 = p_mem_read[reg_pc_plus_2];

  if (p_debug->stats) {
    uint8_t optype = g_optypes[opcode];
    p_debug->count_addr[reg_pc]++;
    p_debug->count_opcode[opcode]++;
    p_debug->count_optype[optype]++;
    p_debug->count_opmode[opmode]++;
  }

  addr_6502 = debug_get_addr(&wrapped_8bit,
                             &wrapped_16bit,
                             opcode,
                             operand1,
                             operand2,
                             reg_x,
                             reg_y,
                             p_mem_read);

  debug_check_unusual(p_debug,
                      opcode,
                      reg_pc,
                      addr_6502,
                      wrapped_8bit,
                      wrapped_16bit);

  hit_break = debug_hit_break(p_debug, reg_pc, addr_6502, opcode);

  if (*p_sigint_received) {
    *p_sigint_received = 0;
    p_debug->debug_running = 0;
  }

  if (p_debug->debug_running && !hit_break && !p_debug->debug_running_print) {
    return 0;
  }

  cycles = state_6502_get_cycles(p_state_6502);

  extra_buf[0] = '\0';
  if (addr_6502 != -1) {
    (void) snprintf(extra_buf,
                    sizeof(extra_buf),
                    "[addr=%.4X val=%.2X]",
                    addr_6502,
                    p_mem_read[addr_6502]);
  } else {
    debug_print_branch(extra_buf,
                       sizeof(extra_buf),
                       opcode,
                       flag_n,
                       flag_o,
                       flag_c,
                       flag_z);
  }

  flag_i = !!(reg_flags & 0x04);
  flag_d = !!(reg_flags & 0x08);

  debug_print_opcode(opcode_buf,
                     sizeof(opcode_buf),
                     opcode,
                     operand1,
                     operand2,
                     reg_pc,
                     do_irq,
                     p_state_6502);

  (void) memset(flags_buf, ' ', 8);
  flags_buf[8] = '\0';
  if (flag_c) {
    flags_buf[0] = 'C';
  }
  if (flag_z) {
    flags_buf[1] = 'Z';
  }
  if (flag_i) {
    flags_buf[2] = 'I';
  }
  if (flag_d) {
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

  debug_print_state(block_6502,
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
    char parse_string[256];
    uint16_t parse_addr;
    int ret;

    int parse_int = -1;
    int parse_int2 = -1;
    int parse_int3 = -1;

    (void) printf("(6502db) ");
    ret = fflush(stdout);
    if (ret != 0) {
      errx(1, "fflush() failed");
    }

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
      (void) printf("print now: %d\n", p_debug->debug_running_print);
    } else if (!strcmp(input_buf, "stats")) {
      p_debug->stats = !p_debug->stats;
      (void) printf("stats now: %d\n", p_debug->stats);
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
      uint8_t stack = reg_s + 1;
      finish_addr = p_mem_read[k_6502_stack_addr + stack];
      stack++;
      finish_addr |= (p_mem_read[k_6502_stack_addr + stack] << 8);
      finish_addr++;
      p_debug->next_or_finish_stop_addr = finish_addr;
      p_debug->debug_running = 1;
      break;
    } else if (sscanf(input_buf, "m %x", &parse_int) == 1) {
      parse_addr = parse_int;
      (void) printf("%04X:", parse_addr);
      for (i = 0; i < 16; ++i) {
        (void) printf(" %02X", p_mem_read[parse_addr]);
        parse_addr++;
      }
      (void) printf("  ");
      parse_addr = parse_int;
      for (i = 0; i < 16; ++i) {
        char c = p_mem_read[parse_addr];
        if (!isprint(c)) {
          c = '.';
        }
        (void) printf("%c", c);
        parse_addr++;
      }
      (void) printf("\n");
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
      bbc_memory_write(p_bbc, parse_int, p_mem_read[parse_int]);
    } else if (sscanf(input_buf, "stopat %x", &parse_int) == 1 &&
               parse_int >= 0 &&
               parse_int < 65536) {
      p_debug->debug_stop_addr = parse_int;
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
    } else if (!strcmp(input_buf, "r")) {
      debug_print_registers(reg_a,
                            reg_x,
                            reg_y,
                            reg_s,
                            flags_buf,
                            reg_pc,
                            cycles);
    } else if (!strcmp(input_buf, "?")) {
      (void) printf(
          "q                : quit\n"
          "c                : continue\n"
          "s                : step one 6502 instuction\n"
          "d <addr>         : disassemble at <addr>\n"
          "t                : trap into gdb\n"
          "b <id> <addr>    : set breakpoint <id> at 6502 address <addr>\n"
          "db <id>          : delete breakpoint <id>\n"
          "bm <id> <lo> (hi): set memory breakpoint for 6502 range\n"
          "dbm <id>         : delete memory breakpoint <id>\n"
          "bop <op>         : break on opcode <op>\n"
          "m <addr>         : show memory at <addr>\n"
          "sm <addr> <val>  : write <val> to 6502 <addr>\n"
          "lm <f> <addr> <l>: load <l> memory at <addr> from state <f>\n"
          "lr <f> <addr>    : load memory at <addr> from raw file <f>\n"
          "ss <f>           : save state to BEM file <f>\n"
          "{a,x,y,pc}=<val> : set register to <val>\n"
          "sys              : show system VIA registers\n"
          "user             : show user VIA registers\n"
          "r                : show regular registers\n");
    } else {
      (void) printf("???\n");
    }
    bbc_set_registers(p_bbc, reg_a, reg_x, reg_y, reg_s, reg_flags, reg_pc);
    ret = fflush(stdout);
    if (ret != 0) {
      errx(1, "fflush() failed");
    }
  }
  if (do_trap) {
    int ret = raise(SIGTRAP);
    if (ret != 0) {
      errx(1, "raise failed");
    }
  }
  return ret_intel_pc;
}
