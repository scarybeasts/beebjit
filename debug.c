#include "debug.h"

#include "bbc.h"
#include "cpu_driver.h"
#include "defs_6502.h"
#include "state.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"
#include "via.h"
#include "video.h"

#include <assert.h>
#include <inttypes.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t k_max_opcode_len = (13 + 1);
static const size_t k_max_extra_len = 32;
enum {
  k_max_break = 16,
};
enum {
  k_max_input_len = 256,
};

enum {
  k_debug_breakpoint_exec = 1,
  k_debug_breakpoint_mem_read = 2,
  k_debug_breakpoint_mem_write = 3,
  k_debug_breakpoint_mem_read_write = 4,
};

struct debug_breakpoint {
  int is_in_use;
  int type;
  int32_t start;
  int32_t end;
  int32_t a_value;
  int32_t x_value;
  int32_t y_value;
};

struct debug_struct {
  struct bbc_struct* p_bbc;
  int debug_active;
  int debug_running;
  int debug_running_print;
  uint8_t* p_opcode_types;
  uint8_t* p_opcode_modes;
  uint8_t* p_opcode_cycles;

  /* Breakpointing. */
  int32_t debug_stop_addr;
  int32_t next_or_finish_stop_addr;
  int debug_break_opcodes[256];
  struct debug_breakpoint breakpoints[k_max_break];

  /* Stats. */
  int stats;
  uint64_t count_addr[k_6502_addr_space_size];
  uint64_t count_opcode[k_6502_op_num_opcodes];
  uint64_t count_optype[k_6502_op_num_types];
  uint64_t count_opmode[k_6502_op_num_modes];
  uint64_t rom_write_faults;
  uint64_t branch_not_taken;
  uint64_t branch_taken;
  uint64_t branch_taken_page_crossing;
  uint64_t abn_reads;
  uint64_t abn_reads_with_page_crossing;
  uint64_t idy_reads;
  uint64_t idy_reads_with_page_crossing;
  uint64_t adc_sbc_count;
  uint64_t adc_sbc_with_decimal_count;
  uint64_t register_reads;
  uint64_t register_writes;

  /* Other. */
  uint8_t warn_at_addr_count[k_6502_addr_space_size];
  char debug_old_input_buf[k_max_input_len];
};

static int s_interrupt_received;
static struct debug_struct* s_p_debug;

static void
debug_interrupt_callback(void) {
  s_interrupt_received = 1;
}

static void
debug_clear_breakpoint(struct debug_struct* p_debug, uint32_t i) {
  p_debug->breakpoints[i].is_in_use = 0;
  p_debug->breakpoints[i].type = 0;
  p_debug->breakpoints[i].start = -1;
  p_debug->breakpoints[i].end = -1;
  p_debug->breakpoints[i].a_value = -1;
  p_debug->breakpoints[i].x_value = -1;
  p_debug->breakpoints[i].y_value = -1;
}

struct debug_struct*
debug_create(struct bbc_struct* p_bbc,
             int debug_active,
             int32_t debug_stop_addr) {
  uint32_t i;
  struct debug_struct* p_debug;

  assert(s_p_debug == NULL);

  p_debug = util_mallocz(sizeof(struct debug_struct));
  /* NOTE: using this singleton pattern for now so we can use qsort().
   * qsort_r() is a minor porting headache due to differing signatures.
   */
  s_p_debug = p_debug;

  util_set_interrupt_callback(debug_interrupt_callback);

  p_debug->p_bbc = p_bbc;
  p_debug->debug_active = debug_active;
  p_debug->debug_running = bbc_get_run_flag(p_bbc);
  p_debug->debug_running_print = bbc_get_print_flag(p_bbc);
  p_debug->debug_stop_addr = debug_stop_addr;
  p_debug->next_or_finish_stop_addr = -1;

  for (i = 0; i < k_max_break; ++i) {
    debug_clear_breakpoint(p_debug, i);
  }

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    p_debug->warn_at_addr_count[i] = 10;
  }

  return p_debug;
}

void
debug_init(struct debug_struct* p_debug) {
  struct cpu_driver* p_cpu_driver = bbc_get_cpu_driver(p_debug->p_bbc);
  p_cpu_driver->p_funcs->get_opcode_maps(p_cpu_driver,
                                         &p_debug->p_opcode_types,
                                         &p_debug->p_opcode_modes,
                                         &p_debug->p_opcode_cycles);
}

void
debug_destroy(struct debug_struct* p_debug) {
  free(p_debug);
}

int
debug_subsystem_active(void* p) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  if (p_debug->debug_active || (p_debug->debug_stop_addr >= 0)) {
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
debug_print_opcode(struct debug_struct* p_debug,
                   char* buf,
                   size_t buf_len,
                   uint8_t opcode,
                   uint8_t operand1,
                   uint8_t operand2,
                   uint16_t reg_pc,
                   int do_irq,
                   struct state_6502* p_state_6502) {
  uint8_t optype;
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

  optype = p_debug->p_opcode_types[opcode];
  opmode = p_debug->p_opcode_modes[opcode];
  opname = g_p_opnames[optype];
  addr = (operand1 | (operand2 << 8));

  switch (opmode) {
  case k_nil:
    (void) snprintf(buf, buf_len, "%s", opname);
    break;
  case k_acc:
    (void) snprintf(buf, buf_len, "%s A", opname);
    break;
  case k_imm:
    (void) snprintf(buf, buf_len, "%s #$%.2"PRIX8, opname, operand1);
    break;
  case k_zpg:
    (void) snprintf(buf, buf_len, "%s $%.2"PRIX8, opname, operand1);
    break;
  case k_abs:
    (void) snprintf(buf, buf_len, "%s $%.4"PRIX16, opname, addr);
    break;
  case k_zpx:
    (void) snprintf(buf, buf_len, "%s $%.2"PRIX8",X", opname, operand1);
    break;
  case k_zpy:
    (void) snprintf(buf, buf_len, "%s $%.2"PRIX8",Y", opname, operand1);
    break;
  case k_abx:
    (void) snprintf(buf, buf_len, "%s $%.4"PRIX16",X", opname, addr);
    break;
  case k_aby:
    (void) snprintf(buf, buf_len, "%s $%.4"PRIX16",Y", opname, addr);
    break;
  case k_idx:
    (void) snprintf(buf, buf_len, "%s ($%.2"PRIX8",X)", opname, operand1);
    break;
  case k_idy:
    (void) snprintf(buf, buf_len, "%s ($%.2"PRIX8"),Y", opname, operand1);
    break;
  case k_ind:
    (void) snprintf(buf, buf_len, "%s ($%.4"PRIX16")", opname, addr);
    break;
  case k_rel:
    addr = (reg_pc + 2 + (char) operand1);
    (void) snprintf(buf, buf_len, "%s $%.4"PRIX16, opname, addr);
    break;
  case k_iax:
    (void) snprintf(buf, buf_len, "%s ($%.4"PRIX16",X)", opname, addr);
    break;
  case k_id:
    (void) snprintf(buf, buf_len, "%s ($%.2"PRIX8")", opname, operand1);
    break;
  case 0:
    (void) snprintf(buf, buf_len, "%s: $%.2"PRIX8, opname, opcode);
    break;
  default:
    assert(0);
    break;
  }
}

static inline void
debug_get_details(int* p_addr_6502,
                  int* p_branch_taken,
                  int* p_is_write,
                  int* p_is_rom,
                  int* p_is_register,
                  int* p_wrapped_8bit,
                  int* p_wrapped_16bit,
                  struct bbc_struct* p_bbc,
                  uint16_t reg_pc,
                  uint8_t opmode,
                  uint8_t optype,
                  uint8_t operand1,
                  uint8_t operand2,
                  uint8_t x_6502,
                  uint8_t y_6502,
                  uint8_t flag_n,
                  uint8_t flag_o,
                  uint8_t flag_c,
                  uint8_t flag_z,
                  uint8_t* p_mem) {
  uint16_t addr_addr;

  int addr = -1;
  int check_wrap_8bit = 1;
  uint8_t opmem = g_opmem[optype];

  *p_addr_6502 = -1;
  *p_branch_taken = -1;
  *p_wrapped_8bit = 0;
  *p_wrapped_16bit = 0;

  switch (opmode) {
  case k_zpg:
    addr = operand1;
    *p_addr_6502 = addr;
    break;
  case k_zpx:
    addr = (operand1 + x_6502);
    *p_addr_6502 = (uint8_t) addr;
    break;
  case k_zpy:
    addr = (operand1 + y_6502);
    *p_addr_6502 = (uint8_t) addr;
    break;
  case k_abs:
    check_wrap_8bit = 0;
    if (optype != k_jsr && optype != k_jmp) {
      addr = (operand1 + (operand2 << 8));
      *p_addr_6502 = addr;
    }
    break;
  case k_abx:
    addr = (operand1 + (operand2 << 8) + x_6502);
    check_wrap_8bit = 0;
    *p_addr_6502 = (uint16_t) addr;
    break;
  case k_aby:
    addr = (operand1 + (operand2 << 8) + y_6502);
    check_wrap_8bit = 0;
    *p_addr_6502 = (uint16_t) addr;
    break;
  case k_idx:
    addr_addr = (operand1 + x_6502);
    addr = (uint8_t) addr_addr;
    if ((addr != addr_addr) || (addr == 0xFF)) {
      *p_wrapped_8bit = 1;
    }
    *p_addr_6502 = p_mem[(uint8_t) (addr + 1)];
    *p_addr_6502 <<= 8;
    *p_addr_6502 |= p_mem[addr];
    addr = *p_addr_6502;
    check_wrap_8bit = 0;
    break;
  case k_idy:
    if (operand1 == 0xFF) {
      *p_wrapped_8bit = 1;
    }
    addr = p_mem[(uint8_t) (operand1 + 1)];
    addr <<= 8;
    addr |= p_mem[operand1];
    addr = (addr + y_6502);
    check_wrap_8bit = 0;
    *p_addr_6502 = (uint16_t) addr;
    break;
  case k_id:
    if (operand1 == 0xFF) {
      *p_wrapped_8bit = 1;
    }
    addr = p_mem[(uint8_t) (operand1 + 1)];
    addr <<= 8;
    addr |= p_mem[operand1];
    *p_addr_6502 = (uint16_t) addr;
    break;
  case k_rel:
    addr_addr = (reg_pc + 2);
    addr = (uint16_t) (addr_addr + (char) operand1);

    switch (optype) {
    case k_bpl:
      *p_branch_taken = !flag_n;
      break;
    case k_bmi:
      *p_branch_taken = flag_n;
      break;
    case k_bvc:
      *p_branch_taken = !flag_o;
      break;
    case k_bvs:
      *p_branch_taken = flag_o;
      break;
    case k_bcc:
      *p_branch_taken = !flag_c;
      break;
    case k_bcs:
      *p_branch_taken = flag_c;
      break;
    case k_bne:
      *p_branch_taken = !flag_z;
      break;
    case k_beq:
      *p_branch_taken = flag_z;
      break;
    case k_bra:
      *p_branch_taken = 1;
      break;
    default:
      assert(0);
      break;
    }
    if (*p_branch_taken) {
      if ((addr_addr & 0x100) ^ (addr & 0x100)) {
        *p_wrapped_8bit = 1;
      }
    }
    break;
  default:
    break;
  }

  if ((opmode != k_rel) && (addr != *p_addr_6502)) {
    if (check_wrap_8bit) {
      *p_wrapped_8bit = 1;
    } else {
      *p_wrapped_16bit = 1;
    }
  }

  *p_is_write = ((opmem == k_write || opmem == k_rw) &&
                 opmode != k_nil &&
                 opmode != k_acc);
  bbc_get_address_details(p_bbc, p_is_register, p_is_rom, *p_addr_6502);
}

static uint16_t
debug_disass(struct debug_struct* p_debug,
             struct cpu_driver* p_cpu_driver,
             struct bbc_struct* p_bbc,
             uint16_t addr_6502) {
  size_t i;

  uint8_t* p_mem_read = bbc_get_mem_read(p_bbc);

  for (i = 0; i < 20; ++i) {
    char opcode_buf[k_max_opcode_len];

    uint16_t addr_plus_1 = (addr_6502 + 1);
    uint16_t addr_plus_2 = (addr_6502 + 2);
    uint8_t opcode = p_mem_read[addr_6502];
    uint8_t opmode = p_debug->p_opcode_modes[opcode];
    uint8_t oplen = g_opmodelens[opmode];
    uint8_t operand1 = p_mem_read[addr_plus_1];
    uint8_t operand2 = p_mem_read[addr_plus_2];

    char* p_address_info = p_cpu_driver->p_funcs->get_address_info(p_cpu_driver,
                                                                   addr_6502);

    debug_print_opcode(p_debug,
                       opcode_buf,
                       sizeof(opcode_buf),
                       opcode,
                       operand1,
                       operand2,
                       addr_6502,
                       0,
                       NULL);
    (void) printf("[%s] %.4"PRIX16": %s\n",
                  p_address_info,
                  addr_6502,
                  opcode_buf);
    addr_6502 += oplen;
  }

  return addr_6502;
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
  int CA1;
  int CA2;
  int CB1;
  int CB2;

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
  via_get_all_CAB(p_via, &CA1, &CA2, &CB1, &CB2);
  (void) printf("IFR %.2"PRIX8" IER %.2"PRIX8"\n", IFR, IER);
  (void) printf("ORA %.2"PRIX8" DDRA %.2"PRIX8" periph %.2"PRIX8"\n",
                ORA,
                DDRA,
                peripheral_a);
  (void) printf("ORB %.2"PRIX8" DDRB %.2"PRIX8" periph %.2"PRIX8"\n",
                ORB,
                DDRB,
                peripheral_b);
  (void) printf("SR %.2"PRIX8" ACR %.2"PRIX8" PCR %.2"PRIX8"\n", SR, ACR, PCR);
  (void) printf("T1L %.4"PRIX16" T1C %.4"PRIX16" oneshot hit %d PB7 %d\n",
                (uint16_t) T1L,
                (uint16_t) (T1C_raw >> 1),
                (int) t1_oneshot_fired,
                (int) t1_pb7);
  (void) printf("T2L %.4"PRIX16" T2C %.4"PRIX16" oneshot hit %d\n",
                (uint16_t) T2L,
                (uint16_t) (T2C_raw >> 1),
                (int) t2_oneshot_fired);
  (void) printf("CA1 %d CA2 %d CB1 %d CB2 %d\n", CA1, CA2, CB1, CB2);
  /* IC32 isn't really a VIA thing but put it here for convenience. */
  if (id == k_via_system) {
    uint8_t IC32 = bbc_get_IC32(p_bbc);
    (void) printf("IC32 %.2"PRIX8"\n", IC32);
  }
}

static void
debug_dump_crtc(struct bbc_struct* p_bbc) {
  uint32_t i;
  uint8_t horiz_counter;
  uint8_t scanline_counter;
  uint8_t vert_counter;
  uint16_t address_counter;
  uint8_t regs[k_video_crtc_num_registers];

  struct video_struct* p_video = bbc_get_video(p_bbc);
  video_get_crtc_state(p_video,
                       &horiz_counter,
                       &scanline_counter,
                       &vert_counter,
                       &address_counter);
  video_get_crtc_registers(p_video, &regs[0]);

  (void) printf("horiz %"PRId8" scanline %"PRId8" vert %"PRId8
                " addr $%.4"PRIX16"\n",
                horiz_counter,
                scanline_counter,
                vert_counter,
                address_counter);
  for (i = 0; i < k_video_crtc_num_registers; ++i) {
    (void) printf("R%.2d $%.2X  ", i, regs[i]);
    if ((i & 7) == 7) {
      (void) printf("\n");
    }
  }
  (void) printf("\n");
}

static struct debug_breakpoint*
debug_get_free_breakpoint(struct debug_struct* p_debug) {
  uint32_t i;

  for (i = 0; i < k_max_break; ++i) {
    struct debug_breakpoint* p_breakpoint = &p_debug->breakpoints[i];
    if (!p_breakpoint->is_in_use) {
      return p_breakpoint;
    }
  }

  return NULL;
}

static inline int
debug_hit_break(struct debug_struct* p_debug,
                uint16_t reg_pc,
                int addr_6502,
                uint8_t opcode_6502,
                uint8_t opmem,
                uint8_t reg_a,
                uint8_t reg_x,
                uint8_t reg_y) {
  uint32_t i;

  for (i = 0; i < k_max_break; ++i) {
    int type;
    struct debug_breakpoint* p_breakpoint = &p_debug->breakpoints[i];
    if (!p_breakpoint->is_in_use) {
      continue;
    }

    if ((p_breakpoint->a_value != -1) && (reg_a != p_breakpoint->a_value)) {
      continue;
    }
    if ((p_breakpoint->x_value != -1) && (reg_x != p_breakpoint->x_value)) {
      continue;
    }
    if ((p_breakpoint->y_value != -1) && (reg_y != p_breakpoint->y_value)) {
      continue;
    }

    type = p_breakpoint->type;
    switch (type) {
    case k_debug_breakpoint_exec:
      if ((reg_pc >= p_breakpoint->start) && (reg_pc <= p_breakpoint->end)) {
        return 1;
      }
      break;
    case k_debug_breakpoint_mem_read:
    case k_debug_breakpoint_mem_write:
    case k_debug_breakpoint_mem_read_write:
      if ((addr_6502 < p_breakpoint->start) ||
          (addr_6502 > p_breakpoint->end)) {
        break;
      }
      if ((opmem == k_read) || (opmem == k_rw)) {
        if ((type == k_debug_breakpoint_mem_read) ||
            (type == k_debug_breakpoint_mem_read_write)) {
          return 1;
        }
      }
      if ((opmem == k_write) || (opmem == k_rw)) {
        if ((type == k_debug_breakpoint_mem_write) ||
            (type == k_debug_breakpoint_mem_read_write)) {
          return 1;
        }
      }
      break;
    default:
      assert(0);
      break;
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
debug_sort_opcodes(const void* p_op1, const void* p_op2) {
  uint8_t op1 = *(uint8_t*) p_op1;
  uint8_t op2 = *(uint8_t*) p_op2;
  return (s_p_debug->count_opcode[op1] - s_p_debug->count_opcode[op2]);
}

static int
debug_sort_addrs(const void* p_addr1, const void* p_addr2) {
  uint16_t addr1 = *(uint16_t*) p_addr1;
  uint16_t addr2 = *(uint16_t*) p_addr2;
  return (s_p_debug->count_addr[addr1] - s_p_debug->count_addr[addr2]);
}

static void
debug_clear_stats(struct debug_struct* p_debug) {
  uint32_t i;
  for (i = 0; i < k_6502_addr_space_size; ++i) {
    p_debug->count_addr[i] = 0;
  }
  for (i = 0; i < k_6502_op_num_opcodes; ++i) {
    p_debug->count_opcode[i] = 0;
  }
  for (i = 0; i < k_6502_op_num_types; ++i) {
    p_debug->count_optype[i] = 0;
  }
  for (i = 0; i < k_6502_op_num_modes; ++i) {
    p_debug->count_opmode[i] = 0;
  }
  p_debug->rom_write_faults = 0;
  p_debug->branch_not_taken = 0;
  p_debug->branch_taken = 0;
  p_debug->branch_taken_page_crossing = 0;
  p_debug->abn_reads = 0;
  p_debug->abn_reads_with_page_crossing = 0;
  p_debug->idy_reads = 0;
  p_debug->idy_reads_with_page_crossing = 0;
  p_debug->adc_sbc_count = 0;
  p_debug->adc_sbc_with_decimal_count = 0;
  p_debug->register_reads = 0;
  p_debug->register_writes = 0;
}

static void
debug_dump_stats(struct debug_struct* p_debug) {
  size_t i;
  uint8_t sorted_opcodes[k_6502_op_num_opcodes];
  uint16_t sorted_addrs[k_6502_addr_space_size];

  for (i = 0; i < k_6502_op_num_opcodes; ++i) {
    sorted_opcodes[i] = i;
  }
  qsort(sorted_opcodes,
        k_6502_op_num_opcodes,
        sizeof(uint8_t),
        debug_sort_opcodes);
  (void) printf("=== Opcodes ===\n");
  for (i = 0; i < k_6502_op_num_opcodes; ++i) {
    char opcode_buf[k_max_opcode_len];
    uint8_t opcode = sorted_opcodes[i];
    uint64_t count = p_debug->count_opcode[opcode];
    if (!count) {
      continue;
    }
    debug_print_opcode(p_debug,
                       opcode_buf,
                       sizeof(opcode_buf),
                       opcode,
                       0,
                       0,
                       0xFFFE,
                       0,
                       NULL);
    (void) printf("%14s: %"PRIu64"\n", opcode_buf, count);
  }

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    sorted_addrs[i] = i;
  }
  qsort(sorted_addrs,
        k_6502_addr_space_size,
        sizeof(uint16_t),
        debug_sort_addrs);
  (void) printf("=== Addrs ===\n");
  for (i = k_6502_addr_space_size - 256; i < k_6502_addr_space_size; ++i) {
    uint16_t addr = sorted_addrs[i];
    uint64_t count = p_debug->count_addr[addr];
    if (!count) {
      continue;
    }
    (void) printf("%4"PRIX16": %"PRIu64"\n", addr, count);
  }
  (void) printf("--> rom_write_faults: %"PRIu64"\n", p_debug->rom_write_faults);
  (void) printf("--> branch (not taken, taken, page cross): "
                "%"PRIu64", %"PRIu64", %"PRIu64"\n",
                p_debug->branch_not_taken,
                p_debug->branch_taken,
                p_debug->branch_taken_page_crossing);
  (void) printf("--> abn reads (total, page crossing): %"PRIu64", %"PRIu64"\n",
                p_debug->abn_reads,
                p_debug->abn_reads_with_page_crossing);
  (void) printf("--> idy reads (total, page crossing): %"PRIu64", %"PRIu64"\n",
                p_debug->idy_reads,
                p_debug->idy_reads_with_page_crossing);
  (void) printf("--> abc/sbc (total, with decimal flag): "
                "%"PRIu64", %"PRIu64"\n",
                p_debug->adc_sbc_count,
                p_debug->adc_sbc_with_decimal_count);
  (void) printf("--> register hits (read / write): %"PRIu64", %"PRIu64"\n",
                p_debug->register_reads,
                p_debug->register_writes);
}

static void
debug_dump_breakpoints(struct debug_struct* p_debug) {
  uint32_t i;
  for (i = 0; i < k_max_break; ++i) {
    const char* p_type_name = NULL;
    struct debug_breakpoint* p_breakpoint = &p_debug->breakpoints[i];
    if (!p_breakpoint->is_in_use) {
      continue;
    }
    (void) printf("breakpoint %"PRIu32": ", i);
    switch (p_breakpoint->type) {
    case k_debug_breakpoint_exec:
      p_type_name = "exec";
      break;
    case k_debug_breakpoint_mem_read:
      p_type_name = "mem read";
      break;
    case k_debug_breakpoint_mem_write:
      p_type_name = "mem write";
      break;
    case k_debug_breakpoint_mem_read_write:
      p_type_name = "mem read/write";
      break;
    default:
      assert(0);
      break;
    }
    (void) printf("%s @$%.4"PRIX16, p_type_name, p_breakpoint->start);
    if (p_breakpoint->end != p_breakpoint->start) {
      (void) printf("-$%.4"PRIX16, p_breakpoint->end);
    }
    (void) printf("\n");
  }
}

static inline void
debug_check_unusual(struct cpu_driver* p_cpu_driver,
                    uint8_t operand1,
                    uint8_t reg_x,
                    uint8_t opmode,
                    uint16_t reg_pc,
                    uint16_t addr_6502,
                    int is_write,
                    int is_rom,
                    int is_register,
                    int wrapped_8bit,
                    int wrapped_16bit) {
  int warned;
  struct debug_struct* p_debug = p_cpu_driver->abi.p_debug_object;
  uint8_t warn_count = p_debug->warn_at_addr_count[reg_pc];

  if (!warn_count) {
    return;
  }

  warned = 0;

  if (is_register && (opmode == k_idx || opmode == k_idy)) {
    (void) printf("DEBUG (UNUSUAL): "
                  "Indirect access to register $%.4"PRIX16" at $%.4"PRIX16"\n",
                  addr_6502,
                  reg_pc);
    warned = 1;
  }

  /* Handled via various means but worth noting. */
  if (is_write && is_rom) {
    (void) printf("DEBUG: Code at $%.4"PRIX16" is writing to ROM "
                  "at $%.4"PRIX16"\n",
                  reg_pc,
                  addr_6502);
    warned = 1;
  }

  /* Look for zero page wrap or full address space wraps. */
  if ((opmode != k_rel) && wrapped_8bit) {
    if (opmode == k_idx) {
      (void) printf("DEBUG (VERY UNUSUAL): "
                    "8-bit IDX ADDRESS WRAP at $%.4"PRIX16" to $%.4"PRIX16"\n",
                    reg_pc,
                    (uint16_t) (uint8_t) (operand1 + reg_x));
      warned = 1;
    } else {
      (void) printf("DEBUG (UNUSUAL): 8-bit ADDRESS WRAP at "
                    "$%.4"PRIX16" to $%.4"PRIX16"\n",
                    reg_pc,
                    addr_6502);
      warned = 1;
    }
  }
  if (wrapped_16bit) {
    (void) printf("DEBUG (VERY UNUSUAL): "
                  "16-bit ADDRESS WRAP at $%.4"PRIX16" to $%.4"PRIX16"\n",
                  reg_pc,
                  addr_6502);
    warned = 1;
  }

  if ((opmode == k_idy || opmode == k_ind) && (operand1 == 0xFF)) {
    (void) printf("DEBUG (PSYCHOTIC): $FF ADDRESS FETCH at $%.4"PRIX16"\n",
                  reg_pc);
    warned = 1;
  } else if (opmode == k_idx && (((uint8_t) (operand1 + reg_x)) == 0xFF)) {
    (void) printf("DEBUG (PSYCHOTIC): $FF ADDRESS FETCH at $%.4"PRIX16"\n",
                  reg_pc);
    warned = 1;
  }

  if (!warned) {
    return;
  }

  warn_count--;
  if (!warn_count) {
    (void) printf("DEBUG: log suppressed for this address\n");
  }

  p_debug->warn_at_addr_count[reg_pc] = warn_count;
}

static void
debug_load_raw(struct debug_struct* p_debug,
               const char* p_file_name,
               uint16_t addr_6502) {
  uint64_t len;
  uint8_t buf[k_6502_addr_space_size];

  struct bbc_struct* p_bbc = p_debug->p_bbc;

  len = util_file_read_fully(p_file_name, buf, sizeof(buf));

  bbc_set_memory_block(p_bbc, addr_6502, len, buf);
}

static void
debug_save_raw(struct debug_struct* p_debug,
               const char* p_file_name,
               uint16_t addr_6502,
               uint16_t length) {
  struct bbc_struct* p_bbc = p_debug->p_bbc;
  uint8_t* p_mem_read = bbc_get_mem_read(p_bbc);

  if ((addr_6502 + length) > k_6502_addr_space_size) {
    length = (k_6502_addr_space_size - addr_6502);
  }

  util_file_write_fully(p_file_name, (p_mem_read + addr_6502), length);
}

static void
debug_print_registers(uint8_t reg_a,
                      uint8_t reg_x,
                      uint8_t reg_y,
                      uint8_t reg_s,
                      const char* flags_buf,
                      uint16_t reg_pc,
                      uint64_t cycles,
                      uint64_t countdown) {
  (void) printf("[A=%.2"PRIX8" X=%.2"PRIX8" Y=%.2"PRIX8" S=%.2"PRIX8" "
                "F=%s PC=%.4"PRIX16" "
                "cycles=%"PRIu64" countdown=%"PRIu64"]\n",
                reg_a,
                reg_x,
                reg_y,
                reg_s,
                flags_buf,
                reg_pc,
                cycles,
                countdown);
}

static void
debug_print_state(char* p_address_info,
                  uint16_t reg_pc,
                  const char* opcode_buf,
                  uint8_t reg_a,
                  uint8_t reg_x,
                  uint8_t reg_y,
                  uint8_t reg_s,
                  const char* flags_buf,
                  const char* extra_buf) {
  (void) printf("[%s] %.4"PRIX16": %-14s "
                "[A=%.2"PRIX8" X=%.2"PRIX8" Y=%.2"PRIX8" S=%.2"PRIX8" F=%s] "
                "%s\n",
                p_address_info,
                reg_pc,
                opcode_buf,
                reg_a,
                reg_x,
                reg_y,
                reg_s,
                flags_buf,
                extra_buf);
}

static uint16_t
debug_parse_number(const char* p_str, int is_hex) {
  int32_t ret = -1;

  if ((p_str[0] == '$') || (p_str[0] == '&')) {
    (void) sscanf((p_str + 1), "%"PRIx32, &ret);
  } else if (is_hex) {
    (void) sscanf(p_str, "%"PRIx32, &ret);
  } else {
    (void) sscanf(p_str, "%"PRId32, &ret);
  }

  return ret;
}

static void
debug_parse_breakpoint(struct debug_breakpoint* p_breakpoint,
                       const char* p_str) {
  char buf[256];
  char c;
  uint16_t value;
  uint32_t len = 0;

  p_breakpoint->is_in_use = 1;
  p_breakpoint->type = k_debug_breakpoint_exec;

  do {
    c = *p_str;
    if ((c == '\0') || isspace(c)) {
      buf[len] = '\0';
      if (len == 0) {
        /* Nothing. */
      } else if (!strcmp(buf, "b") || !strcmp(buf, "break")) {
        /* Nothing. */
      } else if (!strncmp(buf, "a=", 2)) {
        p_breakpoint->a_value = debug_parse_number((buf + 2), 0);
      } else if (!strncmp(buf, "x=", 2)) {
        p_breakpoint->x_value = debug_parse_number((buf + 2), 0);
      } else if (!strncmp(buf, "y=", 2)) {
        p_breakpoint->y_value = debug_parse_number((buf + 2), 0);
      } else {
        value = debug_parse_number(buf, 1);
        if (p_breakpoint->start == -1) {
          p_breakpoint->start = value;
        } else if (p_breakpoint->end == -1) {
          p_breakpoint->end = value;
        }
      }
      len = 0;
    } else {
      if (len < (sizeof(buf) - 1)) {
        buf[len] = c;
        len++;
      }
    }
    p_str++;
  } while (c != '\0');

  if (p_breakpoint->end == -1) {
    p_breakpoint->end = p_breakpoint->start;
  }
}

void*
debug_callback(struct cpu_driver* p_cpu_driver, int do_irq) {
  char opcode_buf[k_max_opcode_len];
  char extra_buf[k_max_extra_len];
  char input_buf[k_max_input_len];
  char flags_buf[9];
  /* NOTE: not correct for execution in hardware registers. */
  uint8_t opcode;
  uint8_t operand1;
  uint8_t operand2;
  int addr_6502;
  int branch_taken;
  int hit_break;
  uint8_t reg_a;
  uint8_t reg_x;
  uint8_t reg_y;
  uint8_t reg_s;
  uint8_t reg_flags;
  uint16_t reg_pc;
  uint16_t reg_pc_plus_1;
  uint16_t reg_pc_plus_2;
  uint8_t flag_z;
  uint8_t flag_n;
  uint8_t flag_c;
  uint8_t flag_o;
  uint8_t flag_i;
  uint8_t flag_d;
  int wrapped_8bit;
  int wrapped_16bit;
  uint8_t opmode;
  uint8_t optype;
  uint8_t opmem;
  uint8_t oplen;
  int is_write;
  int is_rom;
  int is_register;
  char* p_address_info;

  struct debug_struct* p_debug = p_cpu_driver->abi.p_debug_object;
  struct bbc_struct* p_bbc = p_debug->p_bbc;
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);
  uint8_t* p_mem_read = bbc_get_mem_read(p_bbc);
  int do_trap = 0;
  void* ret_intel_pc = 0;
  volatile int* p_interrupt_received = &s_interrupt_received;

  bbc_get_registers(p_bbc, &reg_a, &reg_x, &reg_y, &reg_s, &reg_flags, &reg_pc);
  flag_z = !!(reg_flags & 0x02);
  flag_n = !!(reg_flags & 0x80);
  flag_c = !!(reg_flags & 0x01);
  flag_o = !!(reg_flags & 0x40);
  flag_d = !!(reg_flags & 0x08);

  opcode = p_mem_read[reg_pc];
  if (do_irq) {
    opcode = 0;
  }
  optype = p_debug->p_opcode_types[opcode];
  opmode = p_debug->p_opcode_modes[opcode];

  reg_pc_plus_1 = (reg_pc + 1);
  reg_pc_plus_2 = (reg_pc + 2);
  operand1 = p_mem_read[reg_pc_plus_1];
  operand2 = p_mem_read[reg_pc_plus_2];

  debug_get_details(&addr_6502,
                    &branch_taken,
                    &is_write,
                    &is_rom,
                    &is_register,
                    &wrapped_8bit,
                    &wrapped_16bit,
                    p_bbc,
                    reg_pc,
                    opmode,
                    optype,
                    operand1,
                    operand2,
                    reg_x,
                    reg_y,
                    flag_n,
                    flag_o,
                    flag_c,
                    flag_z,
                    p_mem_read);

  /* If we're about to crash out with an unknown opcode, trap into the
   * debugger.
   */
  if (optype == k_unk) {
    p_debug->debug_running = 0;
  }

  if (p_debug->stats) {
    /* Don't log the address as hit if it was an IRQ. That led to double
     * counting of the address (the second time after RTI). Upon consideration,
     * the stats results are less confusing this way. Specifically, runs of
     * consecutive instructions won't have a surprising double count in the
     * middle.
     */
    if (!do_irq) {
      p_debug->count_addr[reg_pc]++;
    }
    p_debug->count_opcode[opcode]++;
    p_debug->count_optype[optype]++;
    p_debug->count_opmode[opmode]++;
    if (branch_taken == 0) {
      p_debug->branch_not_taken++;
    } else if (branch_taken == 1) {
      p_debug->branch_taken++;
      if (wrapped_8bit) {
        p_debug->branch_taken_page_crossing++;
      }
    }
    if (is_write && is_rom) {
      p_debug->rom_write_faults++;
    }
    if (!is_write) {
      if (opmode == k_abx || opmode == k_aby) {
        p_debug->abn_reads++;
        if ((addr_6502 >> 8) != operand2) {
          p_debug->abn_reads_with_page_crossing++;
        }
      } else if (opmode == k_idy) {
        p_debug->idy_reads++;
        if ((addr_6502 >> 8) != (((uint16_t) (addr_6502 - reg_y)) >> 8)) {
          p_debug->idy_reads_with_page_crossing++;
        }
      }
    }
    if (optype == k_adc || optype == k_sbc) {
      p_debug->adc_sbc_count++;
      if (flag_d) {
        p_debug->adc_sbc_with_decimal_count++;
      }
    }
    if (is_register) {
      if (is_write) {
        p_debug->register_writes++;
      } else {
        p_debug->register_reads++;
      }
    }
  }

  debug_check_unusual(p_cpu_driver,
                      operand1,
                      reg_x,
                      opmode,
                      reg_pc,
                      addr_6502,
                      is_write,
                      is_rom,
                      is_register,
                      wrapped_8bit,
                      wrapped_16bit);

  opmem = g_opmem[optype];
  hit_break = debug_hit_break(p_debug,
                              reg_pc,
                              addr_6502,
                              opcode,
                              opmem,
                              reg_a,
                              reg_x,
                              reg_y);

  if (*p_interrupt_received) {
    *p_interrupt_received = 0;
    p_debug->debug_running = 0;
  }

  if (p_debug->debug_running && !hit_break && !p_debug->debug_running_print) {
    return 0;
  }

  extra_buf[0] = '\0';
  if (addr_6502 != -1) {
    (void) snprintf(extra_buf,
                    sizeof(extra_buf),
                    "[addr=%.4"PRIX16" val=%.2"PRIX8"]",
                    addr_6502,
                    p_mem_read[addr_6502]);
  } else if (branch_taken != -1) {
    if (branch_taken == 0) {
      (void) snprintf(extra_buf, sizeof(extra_buf), "[not taken]");
    } else {
      (void) snprintf(extra_buf, sizeof(extra_buf), "[taken]");
    }
  }

  flag_i = !!(reg_flags & 0x04);

  debug_print_opcode(p_debug,
                     opcode_buf,
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

  p_address_info = p_cpu_driver->p_funcs->get_address_info(p_cpu_driver,
                                                           reg_pc);

  debug_print_state(p_address_info,
                    reg_pc,
                    opcode_buf,
                    reg_a,
                    reg_x,
                    reg_y,
                    reg_s,
                    flags_buf,
                    extra_buf);
  (void) fflush(stdout);

  if (p_debug->debug_running && !hit_break) {
    return 0;
  }

  p_debug->debug_running = 0;
  if (reg_pc == p_debug->next_or_finish_stop_addr) {
    p_debug->next_or_finish_stop_addr = -1;
  }

  oplen = g_opmodelens[opmode];

  while (1) {
    char* input_ret;
    size_t i;
    size_t j;
    char parse_string[256];
    uint16_t parse_addr;
    int ret;
    struct debug_breakpoint* p_breakpoint;

    int32_t parse_int = -1;
    int32_t parse_int2 = -1;

    (void) printf("(6502db) ");
    ret = fflush(stdout);
    if (ret != 0) {
      util_bail("fflush() failed");
    }

    input_ret = fgets(input_buf, sizeof(input_buf), stdin);
    if (input_ret == NULL) {
      util_bail("fgets failed");
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
    } else if (!strcmp(input_buf, "cs")) {
      debug_clear_stats(p_debug);
    } else if (!strcmp(input_buf, "s")) {
      break;
    } else if (!strcmp(input_buf, "t")) {
      do_trap = 1;
      break;
    } else if (!strcmp(input_buf, "c")) {
      p_debug->debug_running = 1;
      break;
    } else if (!strcmp(input_buf, "n")) {
      p_debug->next_or_finish_stop_addr = (reg_pc + oplen);
      p_debug->debug_running = 1;
      break;
    } else if (!strcmp(input_buf, "f")) {
      uint16_t finish_addr;
      uint8_t stack = (reg_s + 1);
      finish_addr = p_mem_read[k_6502_stack_addr + stack];
      stack++;
      finish_addr |= (p_mem_read[k_6502_stack_addr + stack] << 8);
      finish_addr++;
      (void) printf("finish will stop at $%.4"PRIX16"\n", finish_addr);
      p_debug->next_or_finish_stop_addr = finish_addr;
      p_debug->debug_running = 1;
      break;
    } else if (sscanf(input_buf, "m %"PRIx32, &parse_int) == 1) {
      for (j = 0; j < 4; ++j) {
        parse_addr = parse_int;
        (void) printf("%.4"PRIX16":", parse_addr);
        for (i = 0; i < 16; ++i) {
          (void) printf(" %.2"PRIX8, p_mem_read[parse_addr]);
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
        parse_int += 16;
      }
      /* Continue where we left off if just enter is hit next. */
      (void) snprintf(p_debug->debug_old_input_buf,
                      k_max_input_len,
                      "m %x",
                      (uint16_t) parse_int);
    } else if (!strncmp(input_buf, "b ", 2) ||
               !strncmp(input_buf, "break", 5)) {
      p_breakpoint = debug_get_free_breakpoint(p_debug);
      if (p_breakpoint == NULL) {
        (void) printf("no free breakpoints\n");
        continue;
      }
      debug_parse_breakpoint(p_breakpoint, input_buf);
    } else if (!strcmp(input_buf, "bl") || !strcmp(input_buf, "blist")) {
      debug_dump_breakpoints(p_debug);
    } else if (sscanf(input_buf,
                      "bm %"PRIx32" %"PRIx32,
                      &parse_int,
                      &parse_int2) >= 1) {
      parse_addr = parse_int;
      p_breakpoint = debug_get_free_breakpoint(p_debug);
      if (p_breakpoint == NULL) {
        (void) printf("no free breakpoints\n");
        continue;
      }
      p_breakpoint->is_in_use = 1;
      p_breakpoint->type = k_debug_breakpoint_mem_read_write;
      p_breakpoint->start = parse_addr;
      if (parse_int2 != -1) {
        parse_addr = parse_int2;
      }
      p_breakpoint->end = parse_addr;
    } else if (sscanf(input_buf,
                      "bmr %"PRIx32" %"PRIx32,
                      &parse_int,
                      &parse_int2) >= 1) {
      parse_addr = parse_int;
      p_breakpoint = debug_get_free_breakpoint(p_debug);
      if (p_breakpoint == NULL) {
        (void) printf("no free breakpoints\n");
        continue;
      }
      p_breakpoint->is_in_use = 1;
      p_breakpoint->type = k_debug_breakpoint_mem_read;
      p_breakpoint->start = parse_addr;
      if (parse_int2 != -1) {
        parse_addr = parse_int2;
      }
      p_breakpoint->end = parse_addr;
    } else if (sscanf(input_buf,
                      "bmw %"PRIx32" %"PRIx32,
                      &parse_int,
                      &parse_int2) >= 1) {
      parse_addr = parse_int;
      p_breakpoint = debug_get_free_breakpoint(p_debug);
      if (p_breakpoint == NULL) {
        (void) printf("no free breakpoints\n");
        continue;
      }
      p_breakpoint->is_in_use = 1;
      p_breakpoint->type = k_debug_breakpoint_mem_write;
      p_breakpoint->start = parse_addr;
      if (parse_int2 != -1) {
        parse_addr = parse_int2;
      }
      p_breakpoint->end = parse_addr;
    } else if ((sscanf(input_buf, "db %"PRId32, &parse_int) == 1) &&
               (parse_int >= 0) &&
               (parse_int < k_max_break)) {
      debug_clear_breakpoint(p_debug, parse_int);
    } else if ((sscanf(input_buf, "bop %"PRIx32, &parse_int) == 1) &&
               (parse_int >= 0) &&
               (parse_int < 256)) {
      p_debug->debug_break_opcodes[parse_int] = 1;
    } else if ((sscanf(input_buf,
                       "writem %"PRIx32" %"PRIx32,
                       &parse_int,
                       &parse_int2) == 2) &&
               (parse_int >= 0) &&
               (parse_int < 65536)) {
      bbc_memory_write(p_bbc, parse_int, parse_int2);
    } else if ((sscanf(input_buf, "inv %"PRIx32, &parse_int) == 1) &&
               (parse_int >= 0) &&
               (parse_int < 65536)) {
      bbc_memory_write(p_bbc, parse_int, p_mem_read[parse_int]);
    } else if ((sscanf(input_buf, "stopat %"PRIx32, &parse_int) == 1) &&
               (parse_int >= 0) &&
               (parse_int < 65536)) {
      p_debug->debug_stop_addr = parse_int;
    } else if ((sscanf(input_buf,
                      "loadmem %255s %"PRIx32,
                      parse_string,
                      &parse_int) == 2) &&
               (parse_int >= 0) &&
               (parse_int < 65536)) {
      parse_string[255] = '\0';
      debug_load_raw(p_debug, parse_string, parse_int);
    } else if ((sscanf(input_buf,
                      "savemem %255s %"PRIx32" %"PRIx32,
                      parse_string,
                      &parse_int,
                      &parse_int2) == 3) &&
               (parse_int >= 0) &&
               (parse_int < 65536) &&
               (parse_int2 >= 0) &&
               (parse_int2 < 65536)) {
      parse_string[255] = '\0';
      debug_save_raw(p_debug, parse_string, parse_int, parse_int2);
    } else if (sscanf(input_buf, "ss %255s", parse_string) == 1) {
      parse_string[255] = '\0';
      state_save(p_bbc, parse_string);
    } else if (sscanf(input_buf, "a=%"PRIx32, &parse_int) == 1) {
      reg_a = parse_int;
    } else if (sscanf(input_buf, "x=%"PRIx32, &parse_int) == 1) {
      reg_x = parse_int;
    } else if (sscanf(input_buf, "y=%"PRIx32, &parse_int) == 1) {
      reg_y = parse_int;
    } else if (sscanf(input_buf, "s=%"PRIx32, &parse_int) == 1) {
      reg_s = parse_int;
    } else if (sscanf(input_buf, "pc=%"PRIx32, &parse_int) == 1) {
      /* TODO: setting PC broken in JIT mode? */
      reg_pc = parse_int;
    } else if (!strcmp(input_buf, "d") ||
               (sscanf(input_buf, "d %"PRIx32, &parse_int) == 1)) {
      if (parse_int == -1) {
        parse_int = reg_pc;
      }
      parse_addr = debug_disass(p_debug, p_cpu_driver, p_bbc, parse_int);
      /* Continue where we left off if just enter is hit next. */
      (void) snprintf(p_debug->debug_old_input_buf,
                      k_max_input_len,
                      "d %x",
                      parse_addr);
    } else if (!strcmp(input_buf, "sys")) {
      debug_dump_via(p_bbc, k_via_system);
    } else if (!strcmp(input_buf, "user")) {
      debug_dump_via(p_bbc, k_via_user);
    } else if (!strcmp(input_buf, "crtc")) {
      debug_dump_crtc(p_bbc);
    } else if (!strcmp(input_buf, "r")) {
      struct timing_struct* p_timing = bbc_get_timing(p_bbc);
      uint64_t countdown = timing_get_countdown(p_timing);
      uint64_t cycles = state_6502_get_cycles(p_state_6502);
      debug_print_registers(reg_a,
                            reg_x,
                            reg_y,
                            reg_s,
                            flags_buf,
                            reg_pc,
                            cycles,
                            countdown);
    } else if (!strcmp(input_buf, "?") || !strcmp(input_buf, "help")) {
      (void) printf(
  "q                  : quit\n"
  "c, s, n, f         : continue, step (in), next (step over), finish (JSR)\n"
  "d <a>              : disassemble at <a>\n"
  "t                  : trap into gdb\n"
  "{b,break} <a>      : set breakpoint at 6502 address <a>\n"
  "{bl,blist}         : list breakpoints\n"
  "db <id>            : delete breakpoint <id>\n"
  "bm <lo> (hi)       : set read/write memory breakpoint for 6502 range\n"
  "bmr <lo> (hi)      : set read memory breakpoint for 6502 range\n"
  "bmw <lo> (hi)      : set write memory breakpoint for 6502 range\n"
  "bop <op>           : break on opcode <op>\n"
  "m <a>              : show memory at <a>\n"
  "writem <a> <v>     : write <v> to 6502 <a>\n"
  "loadmem <f> <a>    : load memory to <a> from raw file <f>\n"
  "savemem <f> <a> <l>: save memory from <a>, length <l> to raw file <f>\n"
  "ss <f>             : save state to BEM file <f>\n"
  "{a,x,y,pc}=<v>     : set register to <v>\n"
  "sys                : show system VIA registers\n"
  "user               : show user VIA registers\n"
  "r                  : show regular registers\n"
  "stats              : toggle stats collection (default: off)\n"
  "ds                 : dump stats collected\n"
  "cs                 : clear stats collected\n"
  );
    } else {
      (void) printf("???\n");
    }
    bbc_set_registers(p_bbc, reg_a, reg_x, reg_y, reg_s, reg_flags, reg_pc);
    ret = fflush(stdout);
    if (ret != 0) {
      util_bail("fflush() failed");
    }
  }
  if (do_trap) {
    __builtin_trap();
  }
  return ret_intel_pc;
}
