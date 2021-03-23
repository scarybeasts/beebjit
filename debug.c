#include "debug.h"

#include "bbc.h"
#include "cpu_driver.h"
#include "defs_6502.h"
#include "disc_drive.h"
#include "disc_tool.h"
#include "expression.h"
#include "keyboard.h"
#include "log.h"
#include "render.h"
#include "state.h"
#include "state_6502.h"
#include "timing.h"
#include "util.h"
#include "util_string.h"
#include "via.h"
#include "video.h"

#include "os_fault.h"
#include "os_terminal.h"

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
  k_max_input_len = 1024,
};

struct debug_breakpoint {
  int is_in_use;
  int has_exec_range;
  int has_memory_range;
  int is_memory_read;
  int is_memory_write;
  int32_t exec_start;
  int32_t exec_end;
  int32_t memory_start;
  int32_t memory_end;
  int do_print;
  int do_stop;
  char* p_command_list_str;
  struct util_string_list_struct* p_command_list;
  struct expression_struct* p_expression;
};

struct debug_struct {
  struct bbc_struct* p_bbc;
  uint8_t* p_mem_read;
  struct video_struct* p_video;
  struct render_struct* p_render;
  struct disc_tool_struct* p_tool;
  int debug_active;
  int debug_running;
  int debug_running_print;
  uint8_t* p_opcode_types;
  uint8_t* p_opcode_modes;
  uint8_t* p_opcode_mem;
  uint8_t* p_opcode_cycles;
  struct util_string_list_struct* p_command_strings;
  struct util_string_list_struct* p_pending_commands;

  /* Modifiable register / machine state. */
  uint8_t reg_a;
  uint8_t reg_x;
  uint8_t reg_y;
  uint8_t reg_s;
  uint16_t reg_pc;

  /* Breakpointing. */
  int32_t debug_stop_addr;
  int32_t next_or_finish_stop_addr;
  int debug_break_opcodes[256];
  struct debug_breakpoint breakpoints[k_max_break];
  int64_t temp_storage[16];

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
  int32_t timer_id_debug;
  char previous_commands[k_max_input_len];
};

static int s_interrupt_received;
static struct debug_struct* s_p_debug;

static void
debug_interrupt_callback(void) {
  s_interrupt_received = 1;
}

static void
debug_clear_breakpoint(struct debug_struct* p_debug, uint32_t i) {
  struct debug_breakpoint* p_breakpoint = &p_debug->breakpoints[i];
  if (p_breakpoint->p_command_list) {
    util_free(p_breakpoint->p_command_list_str);
    util_string_list_free(p_breakpoint->p_command_list);
  }
  if (p_breakpoint->p_expression) {
    expression_destroy(p_breakpoint->p_expression);
  }
  (void) memset(p_breakpoint, '\0', sizeof(struct debug_breakpoint));
  p_breakpoint->exec_start = -1;
  p_breakpoint->exec_end = -1;
  p_breakpoint->memory_start = -1;
  p_breakpoint->memory_end = -1;
}

static void
debug_timer_callback(void* p) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  struct timing_struct* p_timing = bbc_get_timing(p_debug->p_bbc);
  (void) timing_stop_timer(p_timing, p_debug->timer_id_debug);

  s_interrupt_received = 1;
}

struct debug_struct*
debug_create(struct bbc_struct* p_bbc,
             int debug_active,
             int32_t debug_stop_addr) {
  uint32_t i;
  struct debug_struct* p_debug;
  struct timing_struct* p_timing = bbc_get_timing(p_bbc);

  assert(s_p_debug == NULL);

  p_debug = util_mallocz(sizeof(struct debug_struct));
  /* NOTE: using this singleton pattern for now so we can use qsort().
   * qsort_r() is a minor porting headache due to differing signatures.
   */
  s_p_debug = p_debug;

  os_terminal_set_ctrl_c_callback(debug_interrupt_callback);

  p_debug->p_bbc = p_bbc;
  p_debug->p_mem_read = bbc_get_mem_read(p_bbc);
  p_debug->p_video = bbc_get_video(p_bbc);
  p_debug->p_render = bbc_get_render(p_bbc);
  p_debug->debug_active = debug_active;
  p_debug->debug_running = bbc_get_run_flag(p_bbc);
  p_debug->debug_running_print = bbc_get_print_flag(p_bbc);
  p_debug->debug_stop_addr = debug_stop_addr;
  p_debug->next_or_finish_stop_addr = -1;
  p_debug->p_tool = disc_tool_create();
  p_debug->p_command_strings = util_string_list_alloc();
  p_debug->p_pending_commands = util_string_list_alloc();

  for (i = 0; i < k_max_break; ++i) {
    debug_clear_breakpoint(p_debug, i);
  }

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    p_debug->warn_at_addr_count[i] = 10;
  }

  p_debug->timer_id_debug = timing_register_timer(p_timing,
                                                  debug_timer_callback,
                                                  p_debug);
  return p_debug;
}

void
debug_init(struct debug_struct* p_debug) {
  struct cpu_driver* p_cpu_driver = bbc_get_cpu_driver(p_debug->p_bbc);
  p_cpu_driver->p_funcs->get_opcode_maps(p_cpu_driver,
                                         &p_debug->p_opcode_types,
                                         &p_debug->p_opcode_modes,
                                         &p_debug->p_opcode_mem,
                                         &p_debug->p_opcode_cycles);
}

void
debug_destroy(struct debug_struct* p_debug) {
  disc_tool_destroy(p_debug->p_tool);
  util_string_list_free(p_debug->p_command_strings);
  util_string_list_free(p_debug->p_pending_commands);
  free(p_debug);
}

volatile int*
debug_get_interrupt(struct debug_struct* p_debug) {
  (void) p_debug;
  return &s_interrupt_received;
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
                   int do_irq) {
  uint8_t optype;
  uint8_t opmode;
  const char* opname;
  uint16_t addr;

  if (do_irq) {
    struct state_6502* p_state_6502 = bbc_get_6502(p_debug->p_bbc);
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
  case k_nil1:
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
    addr = (reg_pc + 2 + (int8_t) operand1);
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
                  uint8_t opmem,
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
    addr = (uint16_t) (addr_addr + (int8_t) operand1);

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

  *p_is_write = !!(opmem & k_opmem_write_flag);
  bbc_get_address_details(p_bbc, p_is_register, p_is_rom, *p_addr_6502);
}

static uint16_t
debug_disass(struct debug_struct* p_debug,
             struct cpu_driver* p_cpu_driver,
             uint16_t addr_6502) {
  size_t i;

  uint8_t* p_mem_read = p_debug->p_mem_read;

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
                       0);
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
  uint32_t horiz_pos;
  uint32_t vert_pos;
  struct video_struct* p_video = bbc_get_video(p_bbc);
  struct render_struct* p_render = bbc_get_render(p_bbc);

  horiz_pos = render_get_horiz_pos(p_render);
  vert_pos = render_get_vert_pos(p_render);
  (void) printf("beam pos: horiz %d vert %d\n", horiz_pos, vert_pos);

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

static void
debug_dump_bbc(struct bbc_struct* p_bbc) {
  uint32_t i;
  uint8_t ula_palette[16];
  struct video_struct* p_video = bbc_get_video(p_bbc);
  uint8_t ula_ctrl = video_get_ula_control(p_video);
  video_get_ula_full_palette(p_video, &ula_palette[0]);
  
  (void) printf("ROMSEL $%.2X ACCCON $%.2X IC32 $%.2X\n",
                bbc_get_romsel(p_bbc),
                bbc_get_acccon(p_bbc),
                bbc_get_IC32(p_bbc));
  (void) printf("ULA control $%.2X\n", ula_ctrl);
  (void) printf("ULA palette ");
  for (i = 0; i < 16; ++i) {
    (void) printf("$%.2X ", ula_palette[i]);
  }
  printf("\n");
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

static inline void
debug_check_breakpoints(struct debug_struct* p_debug,
                        int* p_out_print,
                        int* p_out_stop,
                        int addr_6502,
                        uint8_t opcode_6502,
                        uint8_t opmem) {
  uint32_t i;

  int debug_print = 0;
  int debug_stop = 0;

  /* TODO: shouldn't iterate at all if there's no breakpoints. */
  for (i = 0; i < k_max_break; ++i) {
    struct debug_breakpoint* p_breakpoint = &p_debug->breakpoints[i];
    if (!p_breakpoint->is_in_use) {
      continue;
    }

    if (p_breakpoint->has_exec_range) {
      if ((p_debug->reg_pc < p_breakpoint->exec_start) ||
          (p_debug->reg_pc > p_breakpoint->exec_end)) {
        continue;
      }
    }
    if (p_breakpoint->has_memory_range) {
      if ((addr_6502 < p_breakpoint->memory_start) ||
          (addr_6502 > p_breakpoint->memory_end)) {
        continue;
      }
      if (p_breakpoint->is_memory_read && (opmem & k_opmem_read_flag)) {
        /* Match. */
      } else if (p_breakpoint->is_memory_write &&
                 (opmem & k_opmem_write_flag)) {
        /* Match. */
      } else {
        continue;
      }
    }
    if (p_breakpoint->p_expression != NULL) {
      int64_t expression_ret = expression_execute(p_breakpoint->p_expression);
      if (expression_ret == 0) {
        continue;
      }
    }
    /* If we arrive here, it's a hit. */
    debug_print |= p_breakpoint->do_print;
    debug_stop |= p_breakpoint->do_stop;
    if (p_breakpoint->p_command_list != NULL) {
      util_string_list_append_list(p_debug->p_pending_commands,
                                   p_breakpoint->p_command_list);
    }
  }
  if (p_debug->debug_break_opcodes[opcode_6502]) {
    debug_print = 1;
    debug_stop = 1;
  }
  if (p_debug->reg_pc == p_debug->next_or_finish_stop_addr) {
    debug_print = 1;
    debug_stop = 1;
  }
  if (p_debug->reg_pc == p_debug->debug_stop_addr) {
    debug_print = 1;
    debug_stop = 1;
  }

  *p_out_print = debug_print;
  *p_out_stop = debug_stop;
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
                       0);
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
    struct debug_breakpoint* p_breakpoint = &p_debug->breakpoints[i];
    if (!p_breakpoint->is_in_use) {
      continue;
    }
    (void) printf("breakpoint %"PRIu32":", i);
    if (p_breakpoint->has_exec_range) {
      (void) printf(" exec @$%.4"PRIX16"-$%.4"PRIX16,
                    p_breakpoint->exec_start,
                    p_breakpoint->exec_end);
    }
    if (p_breakpoint->has_memory_range) {
      const char* p_name = "read";
      if (p_breakpoint->is_memory_write) {
        if (p_breakpoint->is_memory_read) {
          p_name = "rw";
        } else {
          p_name = "write";
        }
      }
      (void) printf(" %s @$%.4"PRIX16"-$%.4"PRIX16,
                    p_name,
                    p_breakpoint->memory_start,
                    p_breakpoint->memory_end);
    }
    if (p_breakpoint->p_expression != NULL) {
      const char* p_str = expression_get_original_string(
          p_breakpoint->p_expression);
      (void) printf(" expr '%s'", p_str);
    }
    if (p_breakpoint->p_command_list != NULL) {
      (void) printf(" commands '%s'", p_breakpoint->p_command_list_str);
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
  uint8_t* p_mem_read = p_debug->p_mem_read;

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

static int64_t
debug_variable_read_callback(void* p, const char* p_name, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  int64_t ret = 0;

  if (!strcmp(p_name, "a")) {
    ret = p_debug->reg_a;
  } else if (!strcmp(p_name, "x")) {
    ret = p_debug->reg_x;
  } else if (!strcmp(p_name, "y")) {
    ret = p_debug->reg_y;
  } else if (!strcmp(p_name, "s")) {
    ret = p_debug->reg_s;
  } else if (!strcmp(p_name, "pc")) {
    ret = p_debug->reg_pc;
  } else if (!strcmp(p_name, "mem")) {
    ret = -1;
    if (index < k_6502_addr_space_size) {
      ret = p_debug->p_mem_read[index];
    }
  } else if (!strcmp(p_name, "temp")) {
    ret = -1;
    if (index < (sizeof(p_debug->temp_storage) / sizeof(int64_t))) {
      ret = p_debug->temp_storage[index];
    }
  } else if (!strncmp(p_name, "crtc_", 5)) {
    uint8_t horiz_counter;
    uint8_t scanline_counter;
    uint8_t vert_counter;
    uint16_t addr_counter;
    video_get_crtc_state(p_debug->p_video,
                         &horiz_counter,
                         &scanline_counter,
                         &vert_counter,
                         &addr_counter);
    if (!strcmp(p_name, "crtc_c0")) {
      ret = horiz_counter;
    } else if (!strcmp(p_name, "crtc_c4")) {
      ret = vert_counter;
    } else if (!strcmp(p_name, "crtc_c9")) {
      ret = scanline_counter;
    } else if (!strcmp(p_name, "crtc_ma")) {
      ret = addr_counter;
    } else {
      log_do_log(k_log_misc,
                 k_log_warning,
                 "unknown crtc variable: %s",
                 p_name);
    }
  } else if (!strcmp(p_name, "render_x")) {
    video_advance_crtc_timing(p_debug->p_video);
    ret = render_get_horiz_pos(p_debug->p_render);
  } else if (!strcmp(p_name, "render_y")) {
    video_advance_crtc_timing(p_debug->p_video);
    ret = render_get_vert_pos(p_debug->p_render);
  } else {
    log_do_log(k_log_misc, k_log_warning, "unknown read variable: %s", p_name);
  }

  return ret;
}

static void
debug_variable_write_callback(void* p,
                              const char* p_name,
                              uint32_t index,
                              int64_t value) {
  struct debug_struct* p_debug = (struct debug_struct*) p;

  if (!strcmp(p_name, "a")) {
    p_debug->reg_a = value;
  } else if (!strcmp(p_name, "x")) {
    p_debug->reg_x = value;
  } else if (!strcmp(p_name, "y")) {
    p_debug->reg_y = value;
  } else if (!strcmp(p_name, "s")) {
    p_debug->reg_s = value;
  } else if (!strcmp(p_name, "pc")) {
    p_debug->reg_pc = value;
  } else if (!strcmp(p_name, "mem")) {
    if (index < k_6502_addr_space_size) {
      bbc_memory_write(p_debug->p_bbc, index, value);
    }
  } else if (!strcmp(p_name, "temp")) {
    if (index < (sizeof(p_debug->temp_storage) / sizeof(int64_t))) {
      p_debug->temp_storage[index] = value;
    }
  } else if (!strcmp(p_name, "drawline")) {
    struct render_struct* p_render = bbc_get_render(p_debug->p_bbc);
    render_horiz_line(p_render, (uint32_t) value);
  } else {
    log_do_log(k_log_misc, k_log_warning, "unknown write variable: %s", p_name);
  }
}

static void
debug_setup_breakpoint(struct debug_struct* p_debug) {
  uint32_t i_params;
  uint32_t num_params;
  uint16_t value;
  struct util_string_list_struct* p_command_strings;

  struct debug_breakpoint* p_breakpoint = debug_get_free_breakpoint(p_debug);
  int is_memory_range = 0;
  int is_memory_read = 0;
  int is_memory_write = 0;

  if (p_breakpoint == NULL) {
    (void) printf("no free breakpoints\n");
    return;
  }

  p_breakpoint->is_in_use = 1;
  p_breakpoint->do_print = 1;
  p_breakpoint->do_stop = 1;

  p_command_strings = p_debug->p_command_strings;
  num_params = util_string_list_get_count(p_command_strings);
  for (i_params = 0; i_params < num_params; ++i_params) {
    const char* p_param_str;
    /* Skip the "b", "break", "bmw" etc. */
    if (i_params == 0) {
      continue;
    }

    p_param_str = util_string_list_get_string(p_command_strings, i_params);
    if (!strcmp(p_param_str, "noprint")) {
      p_breakpoint->do_print = 0;
    } else if (!strcmp(p_param_str, "nostop")) {
      p_breakpoint->do_stop = 0;
    } else if (!strcmp(p_param_str, "read")) {
      is_memory_range = 1;
      is_memory_read = 1;
    } else if (!strcmp(p_param_str, "write")) {
      is_memory_range = 1;
      is_memory_write = 1;
    } else if (!strcmp(p_param_str, "rw")) {
      is_memory_range = 1;
      is_memory_read = 1;
      is_memory_write = 1;
    } else if (!strcmp(p_param_str, "commands")) {
      if ((i_params + 1) < num_params) {
        const char* p_commands = util_string_list_get_string(p_command_strings,
                                                             (i_params + 1));
        if (p_breakpoint->p_command_list != NULL) {
          util_free(p_breakpoint->p_command_list_str);
          util_string_list_free(p_breakpoint->p_command_list);
        }
        p_breakpoint->p_command_list_str = util_strdup(p_commands);
        p_breakpoint->p_command_list = util_string_list_alloc();
        util_string_split(p_breakpoint->p_command_list, p_commands, ';', '\'');
        i_params++;
      }
    } else if (!strcmp(p_param_str, "expr")) {
      if ((i_params + 1) < num_params) {
        const char* p_expr_str = util_string_list_get_string(p_command_strings,
                                                             (i_params + 1));
        if (!p_breakpoint->p_expression) {
          p_breakpoint->p_expression = expression_create(
              debug_variable_read_callback,
              debug_variable_write_callback,
              p_debug);
        }
        (void) expression_parse(p_breakpoint->p_expression, p_expr_str);
        i_params++;
      }
    } else {
      value = debug_parse_number(p_param_str, 1);
      if (!is_memory_range) {
        p_breakpoint->has_exec_range = 1;
        if (p_breakpoint->exec_start == -1) {
          p_breakpoint->exec_start = value;
        } else if (p_breakpoint->exec_end == -1) {
          p_breakpoint->exec_end = value;
        }
      } else {
        p_breakpoint->has_memory_range = 1;
        p_breakpoint->is_memory_read = is_memory_read;
        p_breakpoint->is_memory_write = is_memory_write;
        if (p_breakpoint->memory_start == -1) {
          p_breakpoint->memory_start = value;
        } else if (p_breakpoint->memory_end == -1) {
          p_breakpoint->memory_end = value;
        }
      }
    }
  }

  if (p_breakpoint->exec_end == -1) {
    p_breakpoint->exec_end = p_breakpoint->exec_start;
  }
  if (p_breakpoint->memory_end == -1) {
    p_breakpoint->memory_end = p_breakpoint->memory_start;
  }
}

static void
debug_print_hex_line(uint8_t* p_buf,
                     uint32_t pos,
                     uint32_t max,
                     uint32_t base) {
  uint32_t i;
  uint32_t num = 16;

  if ((pos + num) < pos) {
    /* Integer overflow. */
    num = 0;
  }
  if ((pos + num) > max) {
    num = (max - pos);
  }

  (void) printf("%.4"PRIX16":", (base + pos));
  for (i = 0; i < num; ++i) {
    (void) printf(" %.2"PRIX8, p_buf[pos + i]);
  }
  (void) printf("  ");
  for (i = 0; i < num; ++i) {
    char c = p_buf[pos + i];
    if (!isprint(c)) {
      c = '.';
    }
    (void) printf("%c", c);
  }
  (void) printf("\n");
}

static void
debug_print_flags_buf(char* p_flags_buf,
                      int flag_n,
                      int flag_o,
                      int flag_c,
                      int flag_z,
                      int flag_i,
                      int flag_d) {
  (void) memset(p_flags_buf, ' ', 8);
  p_flags_buf[8] = '\0';
  if (flag_c) {
    p_flags_buf[0] = 'C';
  }
  if (flag_z) {
    p_flags_buf[1] = 'Z';
  }
  if (flag_i) {
    p_flags_buf[2] = 'I';
  }
  if (flag_d) {
    p_flags_buf[3] = 'D';
  }
  p_flags_buf[5] = '1';
  if (flag_o) {
    p_flags_buf[6] = 'O';
  }
  if (flag_n) {
    p_flags_buf[7] = 'N';
  }
}

static inline void
debug_print_status_line(struct debug_struct* p_debug,
                        struct cpu_driver* p_cpu_driver,
                        int32_t addr_6502,
                        uint8_t opcode,
                        uint8_t operand1,
                        uint8_t operand2,
                        int do_irq,
                        int flag_n,
                        int flag_o,
                        int flag_c,
                        int flag_z,
                        int flag_i,
                        int flag_d,
                        int branch_taken) {
  char flags_buf[9];
  char opcode_buf[k_max_opcode_len];
  char extra_buf[k_max_extra_len];
  char* p_address_info;

  extra_buf[0] = '\0';
  if (addr_6502 != -1) {
    uint8_t* p_mem_read = p_debug->p_mem_read;
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

  debug_print_opcode(p_debug,
                     opcode_buf,
                     sizeof(opcode_buf),
                     opcode,
                     operand1,
                     operand2,
                     p_debug->reg_pc,
                     do_irq);

  debug_print_flags_buf(&flags_buf[0],
                        flag_n,
                        flag_o,
                        flag_c,
                        flag_z,
                        flag_i,
                        flag_d);

  p_address_info = p_cpu_driver->p_funcs->get_address_info(p_cpu_driver,
                                                           p_debug->reg_pc);

  (void) printf("[%s] %.4"PRIX16": %-14s "
                "[A=%.2"PRIX8" X=%.2"PRIX8" Y=%.2"PRIX8" S=%.2"PRIX8" F=%s] "
                "%s\n",
                p_address_info,
                p_debug->reg_pc,
                opcode_buf,
                p_debug->reg_a,
                p_debug->reg_x,
                p_debug->reg_y,
                p_debug->reg_s,
                flags_buf,
                extra_buf);
  (void) fflush(stdout);
}

void*
debug_callback(struct cpu_driver* p_cpu_driver, int do_irq) {
  struct disc_tool_struct* p_tool;
  /* NOTE: not correct for execution in hardware registers. */
  uint8_t opcode;
  uint8_t operand1;
  uint8_t operand2;
  int addr_6502;
  int branch_taken;
  uint8_t reg_flags;
  uint16_t reg_pc_plus_1;
  uint16_t reg_pc_plus_2;
  uint8_t flag_z;
  uint8_t flag_n;
  uint8_t flag_c;
  uint8_t flag_o;
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
  int break_print;
  int break_stop;

  struct debug_struct* p_debug = p_cpu_driver->abi.p_debug_object;
  struct bbc_struct* p_bbc = p_debug->p_bbc;
  uint8_t* p_mem_read = p_debug->p_mem_read;
  int do_trap = 0;
  void* ret_intel_pc = 0;
  volatile int* p_interrupt_received = &s_interrupt_received;

  bbc_get_registers(p_bbc,
                    &p_debug->reg_a,
                    &p_debug->reg_x,
                    &p_debug->reg_y,
                    &p_debug->reg_s,
                    &reg_flags,
                    &p_debug->reg_pc);
  flag_z = !!(reg_flags & 0x02);
  flag_n = !!(reg_flags & 0x80);
  flag_c = !!(reg_flags & 0x01);
  flag_o = !!(reg_flags & 0x40);
  flag_d = !!(reg_flags & 0x08);

  opcode = p_mem_read[p_debug->reg_pc];
  if (do_irq) {
    opcode = 0;
  }
  optype = p_debug->p_opcode_types[opcode];
  opmode = p_debug->p_opcode_modes[opcode];
  opmem = p_debug->p_opcode_mem[opcode];

  reg_pc_plus_1 = (p_debug->reg_pc + 1);
  reg_pc_plus_2 = (p_debug->reg_pc + 2);
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
                    p_debug->reg_pc,
                    opmode,
                    optype,
                    opmem,
                    operand1,
                    operand2,
                    p_debug->reg_x,
                    p_debug->reg_y,
                    flag_n,
                    flag_o,
                    flag_c,
                    flag_z,
                    p_mem_read);

  if (p_debug->stats) {
    /* Don't log the address as hit if it was an IRQ. That led to double
     * counting of the address (the second time after RTI). Upon consideration,
     * the stats results are less confusing this way. Specifically, runs of
     * consecutive instructions won't have a surprising double count in the
     * middle.
     */
    if (!do_irq) {
      p_debug->count_addr[p_debug->reg_pc]++;
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
        if ((addr_6502 >> 8) !=
            (((uint16_t) (addr_6502 - p_debug->reg_y)) >> 8)) {
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
                      p_debug->reg_x,
                      opmode,
                      p_debug->reg_pc,
                      addr_6502,
                      is_write,
                      is_rom,
                      is_register,
                      wrapped_8bit,
                      wrapped_16bit);

  debug_check_breakpoints(p_debug,
                          &break_print,
                          &break_stop,
                          addr_6502,
                          opcode,
                          opmem);

  if (*p_interrupt_received || !p_debug->debug_running) {
    *p_interrupt_received = 0;
    break_print = 1;
    break_stop = 1;
  }

  break_print |= p_debug->debug_running_print;
  if (break_print) {
    int flag_i = !!(reg_flags & 0x04);
    debug_print_status_line(p_debug,
                            p_cpu_driver,
                            addr_6502,
                            opcode,
                            operand1,
                            operand2,
                            do_irq,
                            flag_n,
                            flag_o,
                            flag_c,
                            flag_z,
                            flag_i,
                            flag_d,
                            branch_taken);
  }

  if (break_stop) {
    p_debug->debug_running = 0;
  }

  if (p_debug->debug_running) {
    return 0;
  }

  if (p_debug->reg_pc == p_debug->next_or_finish_stop_addr) {
    p_debug->next_or_finish_stop_addr = -1;
  }

  oplen = g_opmodelens[opmode];

  p_tool = p_debug->p_tool;
  disc_tool_set_disc(p_tool, disc_drive_get_disc(bbc_get_drive_0(p_bbc)));

  while (1) {
    size_t i;
    size_t j;
    char parse_string[256];
    uint8_t disc_data[64];
    uint8_t disc_clocks[64];
    uint16_t parse_addr;
    uint64_t parse_u64;
    int ret;
    char input_buf[k_max_input_len];
    const char* p_command_and_params;
    const char* p_command;

    int32_t parse_int = -1;
    int32_t parse_int2 = -1;
    int32_t parse_int3 = -1;
    int32_t parse_int4 = -1;
    int32_t parse_int5 = -1;
    int32_t parse_int6 = -1;
    int32_t parse_int7 = -1;
    int32_t parse_int8 = -1;
    struct util_string_list_struct* p_pending_commands =
        p_debug->p_pending_commands;
    struct util_string_list_struct* p_command_strings =
        p_debug->p_command_strings;

    (void) printf("(6502db) ");
    ret = fflush(stdout);
    if (ret != 0) {
      util_bail("fflush() failed");
    }

    /* Get more commands from stdin if the list is empty. */
    if (util_string_list_get_count(p_pending_commands) == 0) {
      uint32_t len;
      char* p_input_ret = fgets(&input_buf[0], sizeof(input_buf), stdin);
      if (p_input_ret == NULL) {
        util_bail("fgets failed");
      }
      /* Trim trailing whitespace, most significantly the \n. */
      len = strlen(input_buf);
      while ((len > 0) && isspace(input_buf[len - 1])) {
        input_buf[len - 1] = '\0';
        len--;
      }
      /* If the line is empty (i.e. user just hammers enter), repeat the most
       * recently entered line.
       */
      if (input_buf[0] == '\0') {
        (void) strcpy(&input_buf[0], &p_debug->previous_commands[0]);
      } else {
        (void) strcpy(&p_debug->previous_commands[0], &input_buf[0]);
      }

      util_string_split(p_pending_commands, &input_buf[0], ';', '\'');
    } else {
      (void) printf("%s\n", util_string_list_get_string(p_pending_commands, 0));
    }

    /* Pop command from the front of the pending commands list. */
    p_command_and_params = util_string_list_get_string(p_pending_commands, 0);
    (void) strcpy(&input_buf[0], p_command_and_params);
    util_string_split(p_command_strings, p_command_and_params, ' ', '\'');
    util_string_list_remove(p_pending_commands, 0);

    p_command = "";
    if (util_string_list_get_count(p_command_strings) > 0) {
      p_command = util_string_list_get_string(p_command_strings, 0);
    }

    if (!strcmp(p_command, "q")) {
      exit(0);
    } else if (!strcmp(p_command, "p")) {
      p_debug->debug_running_print = !p_debug->debug_running_print;
      (void) printf("print now: %d\n", p_debug->debug_running_print);
    } else if (!strcmp(p_command, "stats")) {
      p_debug->stats = !p_debug->stats;
      (void) printf("stats now: %d\n", p_debug->stats);
    } else if (!strcmp(p_command, "ds")) {
      debug_dump_stats(p_debug);
    } else if (!strcmp(p_command, "cs")) {
      debug_clear_stats(p_debug);
    } else if (!strcmp(p_command, "s")) {
      break;
    } else if (!strcmp(p_command, "t")) {
      do_trap = 1;
      break;
    } else if (!strcmp(p_command, "c")) {
      p_debug->debug_running = 1;
      break;
    } else if (!strcmp(p_command, "n")) {
      p_debug->next_or_finish_stop_addr = (p_debug->reg_pc + oplen);
      p_debug->debug_running = 1;
      break;
    } else if (!strcmp(p_command, "f")) {
      uint16_t finish_addr;
      uint8_t stack = (p_debug->reg_s + 1);
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
        debug_print_hex_line(p_mem_read, (uint16_t) parse_int, 0x10000, 0);
        parse_int += 16;
      }
      /* Continue where we left off if just enter is hit next. */
      (void) snprintf(&p_debug->previous_commands[0],
                      k_max_input_len,
                      "m %x",
                      (uint16_t) parse_int);
    } else if (sscanf(input_buf, "breakat %"PRIu64, &parse_u64) == 1) {
      uint64_t ticks_delta;
      struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);
      struct timing_struct* p_timing = bbc_get_timing(p_bbc);
      uint32_t timer_id = p_debug->timer_id_debug;
      uint64_t curr_cycles = state_6502_get_cycles(p_state_6502);
      if (timing_timer_is_running(p_timing, timer_id)) {
        (void) timing_stop_timer(p_timing, timer_id);
      }
      if (parse_u64 > curr_cycles) {
        ticks_delta = (parse_u64 - curr_cycles);
        (void) timing_start_timer_with_value(p_timing, timer_id, ticks_delta);
      }
    } else if (!strcmp(p_command, "b") || !strcmp(p_command, "break")) {
      debug_setup_breakpoint(p_debug);
    } else if (!strcmp(p_command, "bm")) {
      util_string_list_insert(p_command_strings, 1, "rw");
      debug_setup_breakpoint(p_debug);
    } else if (!strcmp(p_command, "bmr")) {
      util_string_list_insert(p_command_strings, 1, "read");
      debug_setup_breakpoint(p_debug);
    } else if (!strcmp(p_command, "bmw")) {
      util_string_list_insert(p_command_strings, 1, "write");
      debug_setup_breakpoint(p_debug);
    } else if (!strcmp(p_command, "bl") || !strcmp(p_command, "blist")) {
      debug_dump_breakpoints(p_debug);
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
      p_debug->reg_a = parse_int;
    } else if (sscanf(input_buf, "x=%"PRIx32, &parse_int) == 1) {
      p_debug->reg_x = parse_int;
    } else if (sscanf(input_buf, "y=%"PRIx32, &parse_int) == 1) {
      p_debug->reg_y = parse_int;
    } else if (sscanf(input_buf, "s=%"PRIx32, &parse_int) == 1) {
      p_debug->reg_s = parse_int;
    } else if (sscanf(input_buf, "pc=%"PRIx32, &parse_int) == 1) {
      /* TODO: setting PC broken in JIT mode? */
      p_debug->reg_pc = parse_int;
    } else if (!strcmp(input_buf, "d") ||
               (!strncmp(input_buf, "d ", 2) &&
                    (sscanf(input_buf, "d %"PRIx32, &parse_int) == 1))) {
      if (parse_int == -1) {
        parse_int = p_debug->reg_pc;
      }
      parse_addr = debug_disass(p_debug, p_cpu_driver, parse_int);
      /* Continue where we left off if just enter is hit next. */
      (void) snprintf(&p_debug->previous_commands[0],
                      k_max_input_len,
                      "d %x",
                      parse_addr);
    } else if (!strcmp(p_command, "sys")) {
      debug_dump_via(p_bbc, k_via_system);
    } else if (!strcmp(p_command, "user")) {
      debug_dump_via(p_bbc, k_via_user);
    } else if (!strcmp(p_command, "crtc")) {
      debug_dump_crtc(p_bbc);
    } else if (!strcmp(p_command, "bbc")) {
      debug_dump_bbc(p_bbc);
    } else if (!strcmp(p_command, "r")) {
      char flags_buf[9];
      struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);
      struct timing_struct* p_timing = bbc_get_timing(p_bbc);
      uint64_t countdown = timing_get_countdown(p_timing);
      uint64_t cycles = state_6502_get_cycles(p_state_6502);
      int flag_i = !!(reg_flags & 0x04);
      debug_print_flags_buf(&flags_buf[0],
                            flag_n,
                            flag_o,
                            flag_c,
                            flag_z,
                            flag_i,
                            flag_d);
      debug_print_registers(p_debug->reg_a,
                            p_debug->reg_x,
                            p_debug->reg_y,
                            p_debug->reg_s,
                            flags_buf,
                            p_debug->reg_pc,
                            cycles,
                            countdown);
    } else if ((sscanf(input_buf, "ddrive %"PRId32, &parse_int) == 1) &&
               (parse_int >= 0) &&
               (parse_int <= 3)) {
      struct disc_struct* p_disc;
      struct disc_drive_struct* p_drive;
      if (parse_int & 1) {
        p_drive = bbc_get_drive_1(p_bbc);
      } else {
        p_drive = bbc_get_drive_0(p_bbc);
      }
      p_disc = disc_drive_get_disc(p_drive);
      disc_tool_set_disc(p_tool, p_disc);
      if (parse_int & 2) {
        disc_tool_set_is_side_upper(p_tool, 1);
      } else {
        disc_tool_set_is_side_upper(p_tool, 0);
      }
    } else if ((sscanf(input_buf, "dtrack %"PRId32, &parse_int) == 1) &&
               (parse_int >= 0)) {
      disc_tool_set_track(p_tool, parse_int);
    } else if (!strcmp(p_command, "dsec")) {
      uint32_t i_sectors;
      uint32_t num_sectors;
      struct disc_tool_sector* p_sectors;
      disc_tool_find_sectors(p_tool);
      p_sectors = disc_tool_get_sectors(p_tool, &num_sectors);
      (void) printf("track %d sectors %d",
                    disc_tool_get_track(p_tool),
                    num_sectors);
      for (i_sectors = 0; i_sectors < num_sectors; ++i_sectors) {
        (void) printf(" %s%d[%.2X %.2X %.2X %.2X]",
                      (p_sectors->is_mfm ? "MFM" : "FM"),
                      p_sectors->byte_length,
                      p_sectors->header_bytes[0],
                      p_sectors->header_bytes[1],
                      p_sectors->header_bytes[2],
                      p_sectors->header_bytes[3]);
        p_sectors++;
      }
      (void) printf("\n");
    } else if ((sscanf(input_buf, "drsec %"PRId32, &parse_int) == 1) &&
               (parse_int >= 0)) {
      uint32_t byte_length;
      uint32_t data_chunks;
      uint8_t sector_data[k_disc_tool_max_sector_length];
      disc_tool_read_sector(p_tool,
                            &byte_length,
                            &sector_data[0],
                            parse_int,
                            0);
      data_chunks = (byte_length / 16);
      for (i = 0; i < data_chunks; ++i) {
        debug_print_hex_line(&sector_data[0], (i * 16), byte_length, 0);
      }
    } else if (sscanf(input_buf, "dset %"PRIx8, &parse_int) == 1) {
      disc_tool_fill_fm_data(p_tool, parse_int);
    } else if (!strcmp(input_buf, "dpos") ||
               (sscanf(input_buf, "dpos %"PRId32, &parse_int) == 1)) {
      if (parse_int >= 0) {
        disc_tool_set_byte_pos(p_tool, parse_int);
      } else {
        (void) printf("dpos is %d\n", disc_tool_get_byte_pos(p_tool));
      }
    } else if (!strcmp(input_buf, "drfm") ||
               (sscanf(input_buf, "drfm %"PRId32, &parse_int) == 1)) {
      int is_iffy_pulse;
      if (parse_int >= 0) {
        disc_tool_set_byte_pos(p_tool, parse_int);
      }
      parse_int = disc_tool_get_byte_pos(p_tool);
      disc_tool_read_fm_data(p_tool,
                             &disc_clocks[0],
                             &disc_data[0],
                             &is_iffy_pulse,
                             64);
      for (j = 0; j < 4; ++j) {
        debug_print_hex_line(&disc_data[0], (j * 16), 64, parse_int);
      }
    } else if (!strcmp(input_buf, "drfmc") ||
               (sscanf(input_buf, "drfmc %"PRId32, &parse_int) == 1)) {
      int is_iffy_pulse;
      if (parse_int >= 0) {
        disc_tool_set_byte_pos(p_tool, parse_int);
      }
      parse_int = disc_tool_get_byte_pos(p_tool);
      disc_tool_read_fm_data(p_tool,
                             &disc_clocks[0],
                             &disc_data[0],
                             &is_iffy_pulse,
                             64);
      for (j = 0; j < 4; ++j) {
        debug_print_hex_line(&disc_data[0], (j * 16), 64, parse_int);
        debug_print_hex_line(&disc_clocks[0], (j * 16), 64, parse_int);
      }
    } else if ((strncmp(input_buf, "dwfmc", 5) == 0) &&
               (sscanf(input_buf,
                      "dwfmc %"PRIx8" %"PRIx8,
                       &parse_int,
                       &parse_int2) == 2)) {
      uint8_t data = parse_int;
      uint8_t clocks = parse_int2;
      disc_tool_write_fm_data(p_tool, &clocks, &data, 1);
    } else if ((ret = sscanf(input_buf,
                             "dwfm "
                             "%"PRIx8" %"PRIx8" %"PRIx8" %"PRIx8
                             "%"PRIx8" %"PRIx8" %"PRIx8" %"PRIx8,
                             &parse_int,
                             &parse_int2,
                             &parse_int3,
                             &parse_int4,
                             &parse_int5,
                             &parse_int6,
                             &parse_int7,
                             &parse_int8)) > 0) {
      disc_data[0] = parse_int;
      disc_data[1] = parse_int2;
      disc_data[2] = parse_int3;
      disc_data[3] = parse_int4;
      disc_data[4] = parse_int5;
      disc_data[5] = parse_int6;
      disc_data[6] = parse_int7;
      disc_data[7] = parse_int8;
      disc_tool_write_fm_data(p_tool, NULL, &disc_data[0], ret);
    } else if (sscanf(input_buf, "keydown %"PRId32, &parse_int) == 1) {
      struct keyboard_struct* p_keyboard = bbc_get_keyboard(p_bbc);
      keyboard_system_key_pressed(p_keyboard, (uint8_t) parse_int);
    } else if (sscanf(input_buf, "keyup %"PRId32, &parse_int) == 1) {
      struct keyboard_struct* p_keyboard = bbc_get_keyboard(p_bbc);
      keyboard_system_key_released(p_keyboard, (uint8_t) parse_int);
    } else if (!strcmp(p_command, "?") ||
               !strcmp(p_command, "help") ||
               !strcmp(p_command, "h")) {
      (void) printf(
  "q                  : quit\n"
  "c, s, n, f         : continue, step (in), next (step over), finish (JSR)\n"
  "d <a>              : disassemble at <a>\n"
  "{b,break} <a>      : set breakpoint at 6502 address <a>\n"
  "{bl,blist}         : list breakpoints\n"
  "db <id>            : delete breakpoint <id>\n"
  "bm <lo> (hi)       : set read/write memory breakpoint for 6502 range\n"
  "bmr <lo> (hi)      : set read memory breakpoint for 6502 range\n"
  "bmw <lo> (hi)      : set write memory breakpoint for 6502 range\n"
  "m <a>              : show memory at <a>\n"
  "writem <a> <v>     : write <v> to 6502 <a>\n"
  "loadmem <f> <a>    : load memory to <a> from raw file <f>\n"
  "savemem <f> <a> <l>: save memory from <a>, length <l> to raw file <f>\n"
  "{a,x,y,pc}=<v>     : set register to <v>\n"
  "sys                : show system VIA registers\n"
  "user               : show user VIA registers\n"
  "r                  : show regular registers\n"
  "bbc                : show other BBC registers (ACCCON, ROMSEL, IC32, etc.)\n"
  "disc               : show disc commands\n"
  "more               : show more commands\n"
  );
    } else if (!strcmp(p_command, "disc")) {
      (void) printf(
  "ddrive <d>         : set debug disc drive to <d>\n"
  "dtrack <t>         : set debug disc track to <t>\n"
  "dpos <p>           : set debug disc byte position to <p>\n"
  "drfm (pos)         : read FM encoded data\n"
  "drfmc (pos)        : read FM encoded data, and show clocks too\n"
  "dwfm <...>         : write FM encoded data, up to 8 bytes\n"
  "dwfmc <d> <c>      : write one byte of FM encoded data, with given clocks\n"
  "dset <d>           : fill the track with FM encoded data <d>\n"
  "dsec               : list sector headers of current track\n"
  "drsec <n>          : dump sector in physical order <n> (do dsec first)\n"
  );
    } else if (!strcmp(p_command, "more")) {
      (void) printf(
  "bop <op>           : break on opcode <op>\n"
  "stats              : toggle stats collection (default: off)\n"
  "ds                 : dump stats collected\n"
  "cs                 : clear stats collected\n"
  "t                  : trap into gdb\n"
  "breakat <c>        : break at <c> cycles\n"
  "keydown <k>        : simulate key press <k>\n"
  "keyup <k>          : simulate key release <k>\n"
  "ss <f>             : save state to BEM file <f> (deprecated)\n"
  );
    } else {
      (void) printf("???\n");
    }
    bbc_set_registers(p_bbc,
                      p_debug->reg_a,
                      p_debug->reg_x,
                      p_debug->reg_y,
                      p_debug->reg_s,
                      reg_flags,
                      p_debug->reg_pc);
    ret = fflush(stdout);
    if (ret != 0) {
      util_bail("fflush() failed");
    }
  }
  if (do_trap) {
    os_debug_trap();
  }
  return ret_intel_pc;
}

void
debug_set_commands(struct debug_struct* p_debug, const char* p_commands) {
  struct timing_struct* p_timing = bbc_get_timing(p_debug->p_bbc);

  util_string_split(p_debug->p_pending_commands, p_commands, ';', '\'');
  (void) timing_start_timer_with_value(p_timing, p_debug->timer_id_debug, 1);
}
