#include "debug.h"

#include "bbc.h"
#include "bbc_options.h"
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

enum {
  k_max_opcode_len = (13 + 1),
  k_max_extra_len = 32,
  k_max_break = 16,
  k_max_input_len = 1024,
  k_max_temp_storage = (256 * 1024),
};

struct debug_breakpoint {
  int is_in_use;
  int is_enabled;
  uint64_t num_hits;
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
  struct state_6502* p_state_6502;
  uint8_t* p_mem_read;
  struct timing_struct* p_timing;
  struct video_struct* p_video;
  struct render_struct* p_render;
  struct via_struct* p_system_via;
  struct via_struct* p_user_via;
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
  int opt_is_print_ticks;

  /* Modifiable register / machine state. */
  uint8_t reg_a;
  uint8_t reg_x;
  uint8_t reg_y;
  uint8_t reg_s;
  uint16_t reg_pc;
  /* Other machine state, some calculated. */
  uint8_t reg_flags;
  int32_t addr_6502;
  int is_read;
  int is_write;

  /* Breakpointing. */
  int32_t next_or_finish_stop_addr;
  struct debug_breakpoint breakpoints[k_max_break];
  uint32_t max_breakpoint_used_plus_one;
  struct util_buffer* p_temp_storage_buf;
  int is_sub_instruction_active;
  uint32_t timer_id_sub_instruction;
  uint32_t sub_instruction_tick;
  int32_t breakpoint_continue;
  uint32_t breakpoint_continue_count;

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
  (void) timing_stop_timer(p_debug->p_timing, p_debug->timer_id_debug);

  s_interrupt_received = 1;
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
  util_buffer_destroy(p_debug->p_temp_storage_buf);
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
  return p_debug->debug_active;
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

static inline int
debug_flag_z(uint8_t reg_flags) {
  return !!(reg_flags & 0x02);
}

static inline int
debug_flag_n(uint8_t reg_flags) {
  return !!(reg_flags & 0x80);
}

static inline int
debug_flag_c(uint8_t reg_flags) {
  return !!(reg_flags & 0x01);
}

static inline int
debug_flag_o(uint8_t reg_flags) {
  return !!(reg_flags & 0x40);
}

static inline int
debug_flag_d(uint8_t reg_flags) {
  return !!(reg_flags & 0x08);
}

static inline int
debug_flag_i(uint8_t reg_flags) {
  return !!(reg_flags & 0x04);
}

static inline void
debug_get_details(int* p_addr_6502,
                  int* p_branch_taken,
                  int* p_is_read,
                  int* p_is_write,
                  int* p_is_rom,
                  int* p_is_register,
                  int* p_wrapped_8bit,
                  int* p_wrapped_16bit,
                  struct debug_struct* p_debug,
                  uint8_t opmode,
                  uint8_t optype,
                  uint8_t opmem,
                  uint8_t operand1,
                  uint8_t operand2,
                  uint8_t* p_mem) {
  uint16_t addr_addr;

  int32_t addr = -1;
  int check_wrap_8bit = 1;

  *p_addr_6502 = -1;
  *p_branch_taken = -1;
  *p_wrapped_8bit = 0;
  *p_wrapped_16bit = 0;

  /* This is correct for most modes, but needs care as we want to consider
   * stack operations reads / writes too.
   */
  *p_is_read = !!(opmem & k_opmem_read_flag);
  *p_is_write = !!(opmem & k_opmem_write_flag);

  switch (opmode) {
  case k_zpg:
    addr = operand1;
    *p_addr_6502 = addr;
    break;
  case k_zpx:
    addr = (operand1 + p_debug->reg_x);
    *p_addr_6502 = (uint8_t) addr;
    break;
  case k_zpy:
    addr = (operand1 + p_debug->reg_y);
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
    addr = (operand1 + (operand2 << 8) + p_debug->reg_x);
    check_wrap_8bit = 0;
    *p_addr_6502 = (uint16_t) addr;
    break;
  case k_aby:
    addr = (operand1 + (operand2 << 8) + p_debug->reg_y);
    check_wrap_8bit = 0;
    *p_addr_6502 = (uint16_t) addr;
    break;
  case k_idx:
    addr_addr = (operand1 + p_debug->reg_x);
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
    addr = (addr + p_debug->reg_y);
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
    addr_addr = (p_debug->reg_pc + 2);
    addr = (uint16_t) (addr_addr + (int8_t) operand1);

    switch (optype) {
    case k_bpl:
      *p_branch_taken = !debug_flag_n(p_debug->reg_flags);
      break;
    case k_bmi:
      *p_branch_taken = debug_flag_n(p_debug->reg_flags);
      break;
    case k_bvc:
      *p_branch_taken = !debug_flag_o(p_debug->reg_flags);
      break;
    case k_bvs:
      *p_branch_taken = debug_flag_o(p_debug->reg_flags);
      break;
    case k_bcc:
      *p_branch_taken = !debug_flag_c(p_debug->reg_flags);
      break;
    case k_bcs:
      *p_branch_taken = debug_flag_c(p_debug->reg_flags);
      break;
    case k_bne:
      *p_branch_taken = !debug_flag_z(p_debug->reg_flags);
      break;
    case k_beq:
      *p_branch_taken = debug_flag_z(p_debug->reg_flags);
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
    switch (optype) {
    case k_php:
    case k_pha:
    case k_phx:
    case k_phy:
      *p_is_write = 1;
      addr = (p_debug->reg_s + k_6502_stack_addr);
      *p_addr_6502 = addr;
      break;
    case k_plp:
    case k_pla:
    case k_plx:
    case k_ply:
      *p_is_read = 1;
      addr = ((uint8_t) (p_debug->reg_s + 1) + k_6502_stack_addr);
      *p_addr_6502 = addr;
      break;
    default:
      break;
    }
    break;
  }

  if ((opmode != k_rel) && (addr != *p_addr_6502)) {
    if (check_wrap_8bit) {
      *p_wrapped_8bit = 1;
    } else {
      *p_wrapped_16bit = 1;
    }
  }

  bbc_get_address_details(p_debug->p_bbc,
                          p_is_register,
                          p_is_rom,
                          *p_addr_6502);
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
    const char* p_address_info = "";
    if (p_cpu_driver != NULL) {
      p_address_info = p_cpu_driver->p_funcs->get_address_info(
          p_cpu_driver, addr_6502);
    }

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
  uint64_t crtc_frames;
  int is_in_vert_adjust;
  int is_in_dummy_raster;
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
                       &address_counter,
                       &crtc_frames,
                       &is_in_vert_adjust,
                       &is_in_dummy_raster);
  video_get_crtc_registers(p_video, &regs[0]);

  (void) printf("horiz %"PRId8" scanline %"PRId8" vert %"PRId8
                " addr $%.4"PRIX16" frames %"PRIu64"\n",
                horiz_counter,
                scanline_counter,
                vert_counter,
                address_counter,
                crtc_frames);
  (void) printf("is_in_vert_adjust %d is_in_dummy_raster %d\n",
                is_in_vert_adjust,
                is_in_dummy_raster);
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
      p_breakpoint->is_in_use = 1;
      if ((i + 1) > p_debug->max_breakpoint_used_plus_one) {
        p_debug->max_breakpoint_used_plus_one = (i + 1);
      }
      return p_breakpoint;
    }
  }

  return NULL;
}

static inline void
debug_calculate_max_breakpoint(struct debug_struct* p_debug) {
  uint32_t i;

  p_debug->max_breakpoint_used_plus_one = 0;

  for (i = 0; i < k_max_break; ++i) {
    struct debug_breakpoint* p_breakpoint = &p_debug->breakpoints[i];
    if (p_breakpoint->is_in_use) {
      p_debug->max_breakpoint_used_plus_one = (i + 1);
    }
  }
}

static inline void
debug_check_breakpoints(struct debug_struct* p_debug,
                        int* p_out_print,
                        int* p_out_stop,
                        uint8_t opmem) {
  uint32_t i;
  uint32_t max_breakpoint_used_plus_one = p_debug->max_breakpoint_used_plus_one;

  if (p_debug->reg_pc == p_debug->next_or_finish_stop_addr) {
    *p_out_print = 1;
    *p_out_stop = 1;
  }

  for (i = 0; i < max_breakpoint_used_plus_one; ++i) {
    struct debug_breakpoint* p_breakpoint = &p_debug->breakpoints[i];
    if (!p_breakpoint->is_in_use) {
      continue;
    }
    if (!p_breakpoint->is_enabled) {
      continue;
    }

    if (p_breakpoint->has_exec_range) {
      if ((p_debug->reg_pc < p_breakpoint->exec_start) ||
          (p_debug->reg_pc > p_breakpoint->exec_end)) {
        continue;
      }
    }
    if (p_breakpoint->has_memory_range) {
      if ((p_debug->addr_6502 < p_breakpoint->memory_start) ||
          (p_debug->addr_6502 > p_breakpoint->memory_end)) {
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
    p_breakpoint->num_hits++;
    if (p_breakpoint->do_stop && p_breakpoint->do_print) {
      (void) printf("breakpoint %"PRIu32" hit %"PRIu64" times\n",
                    i,
                    p_breakpoint->num_hits);
    }
    *p_out_print |= p_breakpoint->do_print;
    *p_out_stop |= p_breakpoint->do_stop;
    if (p_breakpoint->p_command_list != NULL) {
      util_string_list_append_list(p_debug->p_pending_commands,
                                   p_breakpoint->p_command_list);
    }
  }
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
                    (uint16_t) p_breakpoint->exec_start,
                    (uint16_t) p_breakpoint->exec_end);
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
                    (uint16_t) p_breakpoint->memory_start,
                    (uint16_t) p_breakpoint->memory_end);
    }
    if (p_breakpoint->p_expression != NULL) {
      const char* p_str = expression_get_original_string(
          p_breakpoint->p_expression);
      (void) printf(" expr '%s'", p_str);
    }
    if (p_breakpoint->p_command_list != NULL) {
      (void) printf(" commands '%s'", p_breakpoint->p_command_list_str);
    }
    if (!p_breakpoint->do_stop) {
      (void) printf(" nostop");
    }
    if (!p_breakpoint->do_print) {
      (void) printf(" noprint");
    }
    if (!p_breakpoint->is_enabled) {
      (void) printf(" disabled");
    }
    (void) printf(" hit %"PRIu64, p_breakpoint->num_hits);
    (void) printf("\n");
  }
}

static inline void
debug_check_unusual(struct debug_struct* p_debug,
                    uint8_t opcode,
                    uint8_t operand1,
                    uint8_t optype,
                    uint8_t opmode,
                    int is_rom,
                    int is_register,
                    int wrapped_8bit,
                    int wrapped_16bit) {
  int warned;
  int is_undocumented = 0;
  uint8_t warn_count = p_debug->warn_at_addr_count[p_debug->reg_pc];

  if (!warn_count) {
    return;
  }

  warned = 0;

  if (is_register && ((opmode == k_idx) || (opmode == k_idy))) {
    (void) printf("DEBUG (UNUSUAL): "
                  "Indirect access to register $%.4"PRIX16" at $%.4"PRIX16"\n",
                  (uint16_t) p_debug->addr_6502,
                  p_debug->reg_pc);
    warned = 1;
  }

  /* Handled via various means but worth noting. */
  if (p_debug->is_write && is_rom) {
    (void) printf("DEBUG: Code at $%.4"PRIX16" is writing to ROM "
                  "at $%.4"PRIX16"\n",
                  p_debug->reg_pc,
                  (uint16_t) p_debug->addr_6502);
    warned = 1;
  }

  /* Look for zero page wrap or full address space wraps. */
  if (wrapped_8bit) {
    if (opmode == k_rel) {
      /* Nothing. */
    } else if (opmode == k_idy) {
      /* Nothing. */
    } else if (opmode == k_idx) {
      uint16_t unwrapped_addr = (operand1 + p_debug->reg_x);
      if (unwrapped_addr >= 0x100) {
        (void) printf("DEBUG (VERY UNUSUAL): 8-bit IDX ADDRESS WRAP at "
                      "$%.4"PRIX16" to $%.4"PRIX16"\n",
                      p_debug->reg_pc,
                      (uint16_t) (uint8_t) unwrapped_addr);
        warned = 1;
      }
    } else {
      (void) printf("DEBUG (UNUSUAL): 8-bit ZPX/Y ADDRESS WRAP at "
                    "$%.4"PRIX16" to $%.4"PRIX16"\n",
                    p_debug->reg_pc,
                    (uint16_t) p_debug->addr_6502);
      warned = 1;
    }
  }
  if (wrapped_16bit) {
    (void) printf("DEBUG (VERY UNUSUAL): "
                  "16-bit ADDRESS WRAP at $%.4"PRIX16" to $%.4"PRIX16"\n",
                  p_debug->reg_pc,
                  (uint16_t) p_debug->addr_6502);
    warned = 1;
  }

  if (((opmode == k_idy) || (opmode == k_ind)) && (operand1 == 0xFF)) {
    (void) printf("DEBUG (PSYCHOTIC): $FF IDY ADDRESS FETCH at $%.4"PRIX16"\n",
                  p_debug->reg_pc);
    warned = 1;
  } else if ((opmode == k_idx) &&
             (((uint8_t) (operand1 + p_debug->reg_x)) == 0xFF)) {
    (void) printf("DEBUG (PSYCHOTIC): $FF IDX ADDRESS FETCH at $%.4"PRIX16"\n",
                  p_debug->reg_pc);
    warned = 1;
  }

  if ((optype >= k_first_6502_undocumented) &&
      (optype <= k_last_6502_undocumented)) {
    is_undocumented = 1;
  } else if ((optype == k_nop) && (opmode != k_nil)) {
    /* NOTE: this doesn't catch all undocumented NOPs, but it catches many. It
     * will miss 1-byte 2-cycle NOPs on 6502 such as $1A.
     */
    is_undocumented = 1;
  }
  if (is_undocumented) {
    (void) printf("DEBUG: undocumented opcode $%.2X at $%.4"PRIX16"\n",
                  opcode,
                  p_debug->reg_pc);
    warned = 1;
  }

  if (!warned) {
    return;
  }

  warn_count--;
  if (!warn_count) {
    (void) printf("DEBUG: log suppressed for this address\n");
  }

  p_debug->warn_at_addr_count[p_debug->reg_pc] = warn_count;
}

static void
debug_load_raw(struct debug_struct* p_debug,
               const char* p_file_name,
               uint16_t addr_6502) {
  uint64_t len;
  uint8_t buf[k_6502_addr_space_size];
  struct util_file* p_file;

  struct bbc_struct* p_bbc = p_debug->p_bbc;

  p_file = util_file_try_open(p_file_name, 0, 0);
  if (p_file == NULL) {
    log_do_log(k_log_misc, k_log_warning, "cannot open file %s", p_file_name);
    return;
  }

  len = util_file_read(p_file, buf, sizeof(buf));
  util_file_close(p_file);

  bbc_set_memory_block(p_bbc, addr_6502, len, buf);
}

static void
debug_save_raw(struct debug_struct* p_debug,
               const char* p_file_name,
               uint16_t addr_6502,
               uint16_t length) {
  struct util_file* p_file;
  uint8_t* p_mem_read = p_debug->p_mem_read;

  if ((addr_6502 + length) > k_6502_addr_space_size) {
    length = (k_6502_addr_space_size - addr_6502);
  }

  p_file = util_file_try_open(p_file_name, 1, 1);
  if (p_file == NULL) {
    log_do_log(k_log_misc, k_log_warning, "cannot open file %s", p_file_name);
    return;
  }

  util_file_write(p_file, (p_mem_read + addr_6502), length);
  util_file_close(p_file);
}

static void
debug_print_registers(uint8_t reg_a,
                      uint8_t reg_x,
                      uint8_t reg_y,
                      uint8_t reg_s,
                      const char* flags_buf,
                      uint16_t reg_pc,
                      uint64_t cycles,
                      uint64_t ticks,
                      uint64_t countdown) {
  (void) printf("6502 [A=%.2"PRIX8" X=%.2"PRIX8" Y=%.2"PRIX8" S=%.2"PRIX8" "
                "F=%s PC=%.4"PRIX16" cycles=%"PRIu64"]\n",
                reg_a,
                reg_x,
                reg_y,
                reg_s,
                flags_buf,
                reg_pc,
                cycles);
  (void) printf("sys  [ticks=%"PRIu64" countdown=%"PRIu64"]\n",
                ticks,
                countdown);
}

static void
debug_make_sub_instruction_active(struct debug_struct* p_debug) {
  if (p_debug->is_sub_instruction_active) {
    return;
  }

  p_debug->is_sub_instruction_active = 1;

  log_do_log(k_log_misc, k_log_info, "sub-instruction ticking activated");

  (void) timing_start_timer_with_value(p_debug->p_timing,
                                       p_debug->timer_id_sub_instruction,
                                       1);
}

static int64_t
debug_read_variable_a(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return p_debug->reg_a;
}

static int64_t
debug_read_variable_x(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return p_debug->reg_x;
}

static int64_t
debug_read_variable_y(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return p_debug->reg_y;
}

static int64_t
debug_read_variable_s(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return p_debug->reg_s;
}

static int64_t
debug_read_variable_pc(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return p_debug->reg_pc;
}

static int64_t
debug_read_variable_flags(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return p_debug->reg_flags;
}

static int64_t
debug_read_variable_ticks(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  uint64_t ticks = timing_get_total_timer_ticks(p_debug->p_timing);
  (void) index;
  return (int64_t) ticks;
}

static int64_t
debug_read_variable_addr(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return p_debug->addr_6502;
}

static int64_t
debug_read_variable_is_read(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return p_debug->is_read;
}

static int64_t
debug_read_variable_is_write(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return p_debug->is_write;
}

static int64_t
debug_read_variable_mem(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  if (index < k_6502_addr_space_size) {
    return p_debug->p_mem_read[index];
  }
  return -1;
}

static int64_t
debug_read_variable_temp(void* p, uint32_t index) {
  uint8_t* p_buf;
  int64_t* p_entry;
  struct debug_struct* p_debug = (struct debug_struct*) p;
  size_t entries = util_buffer_get_length(p_debug->p_temp_storage_buf);
  entries /= sizeof(int64_t);
  if (index >= entries) {
    return 0;
  }
  p_buf = util_buffer_get_ptr(p_debug->p_temp_storage_buf);
  p_entry = (int64_t*) p_buf;
  p_entry += index;
  return *p_entry;
}

static int64_t
debug_read_variable_crtc_r(void* p, uint32_t index) {
  if (index < k_video_crtc_num_registers) {
    struct debug_struct* p_debug = (struct debug_struct*) p;
    uint8_t crtc_regs[k_video_crtc_num_registers];
    video_get_crtc_registers(p_debug->p_video, &crtc_regs[0]);
    return crtc_regs[index];
  }
  return -1;
}

static int64_t
debug_read_variable_crtc_c0(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  uint8_t horiz_counter;
  uint8_t scanline_counter;
  uint8_t vert_counter;
  uint16_t addr_counter;
  uint64_t crtc_frames;
  int is_in_vert_adjust;
  int is_in_dummy_raster;
  (void) index;
  video_get_crtc_state(p_debug->p_video,
                       &horiz_counter,
                       &scanline_counter,
                       &vert_counter,
                       &addr_counter,
                       &crtc_frames,
                       &is_in_vert_adjust,
                       &is_in_dummy_raster);
  return horiz_counter;
}

static int64_t
debug_read_variable_crtc_c4(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  uint8_t horiz_counter;
  uint8_t scanline_counter;
  uint8_t vert_counter;
  uint16_t addr_counter;
  uint64_t crtc_frames;
  int is_in_vert_adjust;
  int is_in_dummy_raster;
  (void) index;
  video_get_crtc_state(p_debug->p_video,
                       &horiz_counter,
                       &scanline_counter,
                       &vert_counter,
                       &addr_counter,
                       &crtc_frames,
                       &is_in_vert_adjust,
                       &is_in_dummy_raster);
  return vert_counter;
}

static int64_t
debug_read_variable_crtc_c9(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  uint8_t horiz_counter;
  uint8_t scanline_counter;
  uint8_t vert_counter;
  uint16_t addr_counter;
  uint64_t crtc_frames;
  int is_in_vert_adjust;
  int is_in_dummy_raster;
  (void) index;
  video_get_crtc_state(p_debug->p_video,
                       &horiz_counter,
                       &scanline_counter,
                       &vert_counter,
                       &addr_counter,
                       &crtc_frames,
                       &is_in_vert_adjust,
                       &is_in_dummy_raster);
  return scanline_counter;
}

static int64_t
debug_read_variable_crtc_ma(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  uint8_t horiz_counter;
  uint8_t scanline_counter;
  uint8_t vert_counter;
  uint16_t addr_counter;
  uint64_t crtc_frames;
  int is_in_vert_adjust;
  int is_in_dummy_raster;
  (void) index;
  video_get_crtc_state(p_debug->p_video,
                       &horiz_counter,
                       &scanline_counter,
                       &vert_counter,
                       &addr_counter,
                       &crtc_frames,
                       &is_in_vert_adjust,
                       &is_in_dummy_raster);
  return addr_counter;
}

static int64_t
debug_read_variable_render_x(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  video_advance_crtc_timing(p_debug->p_video);
  return render_get_horiz_pos(p_debug->p_render);
}

static int64_t
debug_read_variable_render_y(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  video_advance_crtc_timing(p_debug->p_video);
  return render_get_vert_pos(p_debug->p_render);
}

static int64_t
debug_read_variable_sysvia_r(void* p, uint32_t index) {
  if (index < k_via_num_mapped_registers) {
    struct debug_struct* p_debug = (struct debug_struct*) p;
    return via_read_no_side_effects(p_debug->p_system_via, index);
  }
  return -1;
}

static int64_t
debug_read_variable_uservia_r(void* p, uint32_t index) {
  if (index < k_via_num_mapped_registers) {
    struct debug_struct* p_debug = (struct debug_struct*) p;
    return via_read_no_side_effects(p_debug->p_user_via, index);
  }
  return -1;
}

static int64_t
debug_read_variable_irq(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return state_6502_has_irq_high(p_debug->p_state_6502);
}

static int64_t
debug_read_variable_nmi(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return state_6502_has_nmi_high(p_debug->p_state_6502);
}

static int64_t
debug_read_variable_frame_buffer_crc32(void* p, uint32_t index) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  return render_get_buffer_crc32(p_debug->p_render);
}

static int64_t
debug_read_variable_bail(void* p, uint32_t index) {
  (void) p;
  (void) index;
  util_bail("debug bail (variable)");
  return -1;
}

static expression_var_read_func_t
debug_get_read_variable_function(void* p, const char* p_name) {
  expression_var_read_func_t ret = NULL;
  int needs_sub_instruction = 0;

  if (!strcmp(p_name, "a")) {
    ret = debug_read_variable_a;
  } else if (!strcmp(p_name, "x")) {
    ret = debug_read_variable_x;
  } else if (!strcmp(p_name, "y")) {
    ret = debug_read_variable_y;
  } else if (!strcmp(p_name, "s")) {
    ret = debug_read_variable_s;
  } else if (!strcmp(p_name, "pc")) {
    ret = debug_read_variable_pc;
  } else if (!strcmp(p_name, "flags")) {
    ret = debug_read_variable_flags;
  } else if (!strcmp(p_name, "ticks")) {
    ret = debug_read_variable_ticks;
  } else if (!strcmp(p_name, "addr")) {
    ret = debug_read_variable_addr;
  } else if (!strcmp(p_name, "is_read")) {
    ret = debug_read_variable_is_read;
  } else if (!strcmp(p_name, "is_write")) {
    ret = debug_read_variable_is_write;
  } else if (!strcmp(p_name, "mem")) {
    ret = debug_read_variable_mem;
  } else if (!strcmp(p_name, "temp")) {
    ret = debug_read_variable_temp;
  } else if (!strcmp(p_name, "crtc_r")) {
    ret = debug_read_variable_crtc_r;
  } else if (!strcmp(p_name, "crtc_c0")) {
    needs_sub_instruction = 1;
    ret = debug_read_variable_crtc_c0;
  } else if (!strcmp(p_name, "crtc_c4")) {
    needs_sub_instruction = 1;
    ret = debug_read_variable_crtc_c4;
  } else if (!strcmp(p_name, "crtc_c9")) {
    needs_sub_instruction = 1;
    ret = debug_read_variable_crtc_c9;
  } else if (!strcmp(p_name, "crtc_ma")) {
    needs_sub_instruction = 1;
    ret = debug_read_variable_crtc_ma;
  } else if (!strcmp(p_name, "render_x")) {
    needs_sub_instruction = 1;
    ret = debug_read_variable_render_x;
  } else if (!strcmp(p_name, "render_y")) {
    needs_sub_instruction = 1;
    ret = debug_read_variable_render_y;
  } else if (!strcmp(p_name, "sysvia_r")) {
    needs_sub_instruction = 1;
    ret = debug_read_variable_sysvia_r;
  } else if (!strcmp(p_name, "uservia_r")) {
    needs_sub_instruction = 1;
    ret = debug_read_variable_uservia_r;
  } else if (!strcmp(p_name, "irq")) {
    needs_sub_instruction = 1;
    ret = debug_read_variable_irq;
  } else if (!strcmp(p_name, "nmi")) {
    needs_sub_instruction = 1;
    ret = debug_read_variable_nmi;
  } else if (!strcmp(p_name, "frame_buffer_crc32")) {
    ret = debug_read_variable_frame_buffer_crc32;
  } else if (!strcmp(p_name, "bail")) {
    ret = debug_read_variable_bail;
  }

  if (needs_sub_instruction) {
    struct debug_struct* p_debug = (struct debug_struct*) p;
    debug_make_sub_instruction_active(p_debug);
  }

  return ret;
}

static void
debug_write_variable_a(void* p, uint32_t index, int64_t value) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  p_debug->reg_a = value;
}

static void
debug_write_variable_x(void* p, uint32_t index, int64_t value) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  p_debug->reg_x = value;
}

static void
debug_write_variable_y(void* p, uint32_t index, int64_t value) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  p_debug->reg_y = value;
}

static void
debug_write_variable_s(void* p, uint32_t index, int64_t value) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  p_debug->reg_s = value;
}

static void
debug_write_variable_pc(void* p, uint32_t index, int64_t value) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  (void) index;
  p_debug->reg_pc = value;
}

static void
debug_write_variable_mem(void* p, uint32_t index, int64_t value) {
  if (index < k_6502_addr_space_size) {
    struct debug_struct* p_debug = (struct debug_struct*) p;
    bbc_memory_write(p_debug->p_bbc, index, value);
  }
}

static void
debug_write_variable_temp(void* p, uint32_t index, int64_t value) {
  uint8_t* p_buf;
  int64_t* p_entry;
  struct debug_struct* p_debug = (struct debug_struct*) p;

  if (index >= k_max_temp_storage) {
    return;
  }
  util_buffer_ensure_capacity(p_debug->p_temp_storage_buf,
                              ((index + 1) * sizeof(int64_t)));
  p_buf = util_buffer_get_ptr(p_debug->p_temp_storage_buf);
  p_entry = (int64_t*) p_buf;
  p_entry += index;
  *p_entry = value;
}

static void
debug_write_variable_drawline(void* p, uint32_t index, int64_t value) {
  struct debug_struct* p_debug = (struct debug_struct*) p;
  struct render_struct* p_render = bbc_get_render(p_debug->p_bbc);
  (void) index;
  render_horiz_line(p_render, (uint32_t) value);
}

static void
debug_write_variable_sysvia_r(void* p, uint32_t index, int64_t value) {
  if (index < k_via_num_mapped_registers) {
    struct debug_struct* p_debug = (struct debug_struct*) p;
    via_write(p_debug->p_system_via, index, (uint8_t) value);
  }
}

static void
debug_write_variable_uservia_r(void* p, uint32_t index, int64_t value) {
  if (index < k_via_num_mapped_registers) {
    struct debug_struct* p_debug = (struct debug_struct*) p;
    via_write(p_debug->p_user_via, index, (uint8_t) value);
  }
}

static expression_var_write_func_t
debug_get_write_variable_function(void* p, const char* p_name) {
  expression_var_write_func_t ret = NULL;
  (void) p;

  if (!strcmp(p_name, "a")) {
    ret = debug_write_variable_a;
  } else if (!strcmp(p_name, "x")) {
    ret = debug_write_variable_x;
  } else if (!strcmp(p_name, "y")) {
    ret = debug_write_variable_y;
  } else if (!strcmp(p_name, "s")) {
    ret = debug_write_variable_s;
  } else if (!strcmp(p_name, "pc")) {
    ret = debug_write_variable_pc;
  } else if (!strcmp(p_name, "mem")) {
    ret = debug_write_variable_mem;
  } else if (!strcmp(p_name, "temp")) {
    ret = debug_write_variable_temp;
  } else if (!strcmp(p_name, "drawline")) {
    ret = debug_write_variable_drawline;
  } else if (!strcmp(p_name, "sysvia_r")) {
    ret = debug_write_variable_sysvia_r;
  } else if (!strcmp(p_name, "uservia_r")) {
    ret = debug_write_variable_uservia_r;
  }

  return ret;
}

static void
debug_setup_breakpoint(struct debug_struct* p_debug) {
  uint32_t i_params;
  uint32_t num_params;
  int32_t value;
  struct util_string_list_struct* p_command_strings;

  struct debug_breakpoint* p_breakpoint = debug_get_free_breakpoint(p_debug);
  int is_memory_range = 0;
  int is_memory_read = 0;
  int is_memory_write = 0;

  if (p_breakpoint == NULL) {
    (void) printf("no free breakpoints\n");
    return;
  }

  assert(p_breakpoint->is_in_use);
  p_breakpoint->is_enabled = 1;
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
    } else if (!strcmp(p_param_str, "disabled")) {
      p_breakpoint->is_enabled = 0;
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
              debug_get_read_variable_function,
              debug_get_write_variable_function,
              p_debug);
        }
        (void) expression_parse(p_breakpoint->p_expression, p_expr_str);
        i_params++;
      }
    } else {
      value = (int32_t) util_parse_u64(p_param_str, 1);
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

  (void) printf("%.4"PRIX16":", (uint16_t) (base + pos));
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
debug_print_flags_buf(char* p_flags_buf, uint8_t reg_flags) {
  (void) memset(p_flags_buf, ' ', 8);
  p_flags_buf[8] = '\0';
  if (debug_flag_c(reg_flags)) {
    p_flags_buf[0] = 'C';
  }
  if (debug_flag_z(reg_flags)) {
    p_flags_buf[1] = 'Z';
  }
  if (debug_flag_i(reg_flags)) {
    p_flags_buf[2] = 'I';
  }
  if (debug_flag_d(reg_flags)) {
    p_flags_buf[3] = 'D';
  }
  p_flags_buf[5] = '1';
  if (debug_flag_o(reg_flags)) {
    p_flags_buf[6] = 'O';
  }
  if (debug_flag_n(reg_flags)) {
    p_flags_buf[7] = 'N';
  }
}

static inline void
debug_print_status_line(struct debug_struct* p_debug,
                        struct cpu_driver* p_cpu_driver,
                        uint8_t opcode,
                        uint8_t operand1,
                        uint8_t operand2,
                        int do_irq,
                        int branch_taken) {
  char sub_tag[5];
  char flags_buf[9];
  char opcode_buf[k_max_opcode_len];
  char extra_buf[k_max_extra_len];
  char ticks_buf[32];
  const char* p_address_info;
  uint8_t reg_flags = p_debug->reg_flags;

  extra_buf[0] = '\0';
  if (p_debug->addr_6502 != -1) {
    uint8_t* p_mem_read = p_debug->p_mem_read;
    (void) snprintf(extra_buf,
                    sizeof(extra_buf),
                    "[addr=%.4"PRIX16" val=%.2"PRIX8"]",
                    (uint16_t) p_debug->addr_6502,
                    p_mem_read[p_debug->addr_6502]);
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

  debug_print_flags_buf(&flags_buf[0], reg_flags);

  if (p_cpu_driver != NULL) {
    p_address_info = p_cpu_driver->p_funcs->get_address_info(p_cpu_driver,
                                                             p_debug->reg_pc);
  } else {
    assert(p_debug->is_sub_instruction_active);
    (void) snprintf(sub_tag,
                    sizeof(sub_tag),
                    "SUB%d",
                    p_debug->sub_instruction_tick);
    p_address_info = &sub_tag[0];
  }

  if (p_debug->opt_is_print_ticks) {
    snprintf(ticks_buf,
             sizeof(ticks_buf),
             "[ticks=%"PRIu64"] ",
             timing_get_total_timer_ticks(p_debug->p_timing));
  } else {
    ticks_buf[0] = '\0';
  }

  (void) printf("[%s] %.4"PRIX16": %-14s "
                "[A=%.2"PRIX8" X=%.2"PRIX8" Y=%.2"PRIX8" S=%.2"PRIX8" F=%s] "
                "%s%s\n",
                p_address_info,
                p_debug->reg_pc,
                opcode_buf,
                p_debug->reg_a,
                p_debug->reg_x,
                p_debug->reg_y,
                p_debug->reg_s,
                flags_buf,
                ticks_buf,
                extra_buf);
  (void) fflush(stdout);
}

static void*
debug_callback_common(struct debug_struct* p_debug,
                      struct cpu_driver* p_cpu_driver,
                      int do_irq) {
  struct disc_tool_struct* p_tool;
  /* NOTE: not correct for execution in hardware registers. */
  uint8_t opcode;
  uint8_t operand1;
  uint8_t operand2;
  int branch_taken;
  uint16_t reg_pc_plus_1;
  uint16_t reg_pc_plus_2;
  int wrapped_8bit;
  int wrapped_16bit;
  uint8_t opmode;
  uint8_t optype;
  uint8_t opmem;
  uint8_t oplen;
  int is_rom;
  int is_register;
  uint64_t breakpoint_continue_hit_count;
  struct debug_breakpoint* p_breakpoint_continue;
  struct bbc_struct* p_bbc;

  struct state_6502* p_state_6502 = p_debug->p_state_6502;
  uint8_t* p_mem_read = p_debug->p_mem_read;
  int do_trap = 0;
  void* ret_intel_pc = 0;
  volatile int* p_interrupt_received = &s_interrupt_received;
  int break_print = 0;
  int break_stop = 0;

  state_6502_get_registers(p_state_6502,
                           &p_debug->reg_a,
                           &p_debug->reg_x,
                           &p_debug->reg_y,
                           &p_debug->reg_s,
                           &p_debug->reg_flags,
                           &p_debug->reg_pc);

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

  debug_get_details(&p_debug->addr_6502,
                    &branch_taken,
                    &p_debug->is_read,
                    &p_debug->is_write,
                    &is_rom,
                    &is_register,
                    &wrapped_8bit,
                    &wrapped_16bit,
                    p_debug,
                    opmode,
                    optype,
                    opmem,
                    operand1,
                    operand2,
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
    if (p_debug->is_write && is_rom) {
      p_debug->rom_write_faults++;
    }
    if (!p_debug->is_write) {
      if (opmode == k_abx || opmode == k_aby) {
        p_debug->abn_reads++;
        if ((p_debug->addr_6502 >> 8) != operand2) {
          p_debug->abn_reads_with_page_crossing++;
        }
      } else if (opmode == k_idy) {
        p_debug->idy_reads++;
        if ((p_debug->addr_6502 >> 8) !=
            (((uint16_t) (p_debug->addr_6502 - p_debug->reg_y)) >> 8)) {
          p_debug->idy_reads_with_page_crossing++;
        }
      }
    }
    if (optype == k_adc || optype == k_sbc) {
      p_debug->adc_sbc_count++;
      if (debug_flag_d(p_debug->reg_flags)) {
        p_debug->adc_sbc_with_decimal_count++;
      }
    }
    if (is_register) {
      if (p_debug->is_write) {
        p_debug->register_writes++;
      } else {
        p_debug->register_reads++;
      }
    }
  }

  debug_check_unusual(p_debug,
                      opcode,
                      operand1,
                      optype,
                      opmode,
                      is_rom,
                      is_register,
                      wrapped_8bit,
                      wrapped_16bit);

  breakpoint_continue_hit_count = 0;
  p_breakpoint_continue = NULL;
  if (p_debug->breakpoint_continue != -1) {
    p_breakpoint_continue = &p_debug->breakpoints[p_debug->breakpoint_continue];
    breakpoint_continue_hit_count = p_breakpoint_continue->num_hits;
  }
  debug_check_breakpoints(p_debug,
                          &break_print,
                          &break_stop,
                          opmem);
  if ((p_breakpoint_continue != NULL) &&
      (p_breakpoint_continue->num_hits > breakpoint_continue_hit_count)) {
    assert(p_debug->breakpoint_continue_count > 0);
    p_debug->breakpoint_continue_count--;
    if (p_debug->breakpoint_continue_count > 0) {
      break_stop = 0;
    } else {
      p_debug->breakpoint_continue = -1;
    }
  }

  if (*p_interrupt_received || !p_debug->debug_running) {
    *p_interrupt_received = 0;
    break_print = 1;
    break_stop = 1;
  }

  break_print |= p_debug->debug_running_print;
  if (break_print) {
    debug_print_status_line(p_debug,
                            p_cpu_driver,
                            opcode,
                            operand1,
                            operand2,
                            do_irq,
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

  p_bbc = p_debug->p_bbc;
  p_tool = p_debug->p_tool;
  disc_tool_set_disc(p_tool, disc_drive_get_disc(bbc_get_drive_0(p_bbc)));

  if (!p_debug->debug_active) {
    log_do_log(k_log_misc,
               k_log_info,
               "running without -debug; some commands won't work");
  }

  while (1) {
    size_t i;
    size_t j;
    uint8_t disc_data[64];
    uint8_t disc_clocks[64];
    uint16_t parse_addr;
    int ret;
    char input_buf[k_max_input_len];
    uint32_t num_strings;
    const char* p_command_and_params;
    const char* p_command;
    const char* p_param_str;
    const char* p_param_1_str;
    const char* p_param_2_str;
    const char* p_param_3_str;
    int32_t parse_int = -1;
    int32_t parse_int2 = -1;
    int32_t parse_hex_int = -1;
    int32_t parse_hex_int2 = -1;
    int32_t parse_hex_int3 = -1;
    uint64_t parse_u64_int = 0;
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
      char* p_input_ret;
      uint32_t len;

      /* If we're blocking waiting on user input, re-paint the screen. This
       * gives the expected result if the user is stepping, for example,
       * scanline by scanline.
       */
      video_advance_crtc_timing(p_debug->p_video);
      if (render_has_buffer(p_debug->p_render)) {
        video_force_paint(p_debug->p_video, 0);
      }

      p_input_ret = fgets(&input_buf[0], sizeof(input_buf), stdin);
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
    num_strings = util_string_list_get_count(p_command_strings);
    if (num_strings > 0) {
      p_command = util_string_list_get_string(p_command_strings, 0);
    }
    p_param_1_str = NULL;
    if (num_strings > 1) {
      p_param_1_str = util_string_list_get_string(p_command_strings, 1);
      parse_u64_int = util_parse_u64(p_param_1_str, 0);
      parse_int = (int32_t) parse_u64_int;
      parse_hex_int = (int32_t) util_parse_u64(p_param_1_str, 1);
    }
    p_param_2_str = NULL;
    if (num_strings > 2) {
      p_param_2_str = util_string_list_get_string(p_command_strings, 2);
      parse_int2 = (int32_t) util_parse_u64(p_param_2_str, 0);
      parse_hex_int2 = (int32_t) util_parse_u64(p_param_2_str, 1);
    }
    p_param_3_str = NULL;
    if (num_strings > 3) {
      p_param_3_str = util_string_list_get_string(p_command_strings, 3);
      parse_hex_int3 = (int32_t) util_parse_u64(p_param_3_str, 1);
    }

    if (!strcmp(p_command, "q")) {
      exit(0);
    } else if (!strcmp(p_command, "bail")) {
      util_bail("debug bail (command)");
    } else if (!strcmp(p_command, "p")) {
      p_debug->debug_running_print = !p_debug->debug_running_print;
      (void) printf("print now: %d\n", p_debug->debug_running_print);
    } else if (!strcmp(p_command, "fast")) {
      int is_fast = bbc_get_fast_flag(p_bbc);
      is_fast = !is_fast;
      bbc_set_fast_flag(p_bbc, is_fast);
      (void) printf("fast now: %d\n", is_fast);
    } else if (!strcmp(p_command, "stats")) {
      p_debug->stats = !p_debug->stats;
      (void) printf("stats now: %d\n", p_debug->stats);
    } else if (!strcmp(p_command, "ds")) {
      debug_dump_stats(p_debug);
    } else if (!strcmp(p_command, "cs")) {
      debug_clear_stats(p_debug);
    } else if (!strcmp(p_command, "s") || !strcmp(p_command, "step")) {
      break;
    } else if (!strcmp(p_command, "t")) {
      do_trap = 1;
      break;
    } else if (!strcmp(p_command, "c")) {
      p_debug->debug_running = 1;
      break;
    } else if (!strcmp(p_command, "bc")) {
      p_debug->debug_running = 1;
      if ((parse_int >= 0) &&
          (parse_int < k_max_break) &&
          p_debug->breakpoints[parse_int].is_in_use &&
          (parse_int2 > 0)) {
        p_debug->breakpoint_continue = parse_int;
        p_debug->breakpoint_continue_count = parse_int2;
      }
      break;
    } else if (!strcmp(p_command, "n") || !strcmp(p_command, "next")) {
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
    } else if (!strcmp(p_command, "m")) {
      for (j = 0; j < 4; ++j) {
        debug_print_hex_line(p_mem_read, (uint16_t) parse_hex_int, 0x10000, 0);
        parse_hex_int += 16;
      }
      /* Continue where we left off if just enter is hit next. */
      (void) snprintf(&p_debug->previous_commands[0],
                      k_max_input_len,
                      "m %x",
                      (uint16_t) parse_hex_int);
    } else if (!strcmp(p_command, "breakat")) {
      uint64_t ticks_delta;
      struct timing_struct* p_timing = p_debug->p_timing;
      uint32_t timer_id = p_debug->timer_id_debug;
      uint64_t ticks = timing_get_total_timer_ticks(p_timing);
      if (timing_timer_is_running(p_timing, timer_id)) {
        (void) timing_stop_timer(p_timing, timer_id);
      }
      if (parse_u64_int > ticks) {
        ticks_delta = (parse_u64_int - ticks);
        (void) timing_start_timer_with_value(p_timing, timer_id, ticks_delta);
      }
    } else if (!strcmp(p_command, "seek")) {
      (void) bbc_replay_seek(p_bbc, (parse_int * 2000000ull));
      p_debug->debug_running = 1;
      break;
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
    } else if (!strcmp(p_command, "db") &&
               (parse_int >= 0) &&
               (parse_int < k_max_break)) {
      debug_clear_breakpoint(p_debug, parse_int);
      debug_calculate_max_breakpoint(p_debug);
    } else if (!strcmp(p_command, "enable") &&
               (parse_int >= 0) &&
               (parse_int < k_max_break)) {
      struct debug_breakpoint* p_breakpoint = &p_debug->breakpoints[parse_int];
      if (p_breakpoint->is_in_use) {
        p_breakpoint->is_enabled = 1;
      }
    } else if (!strcmp(p_command, "disable") &&
               (parse_int >= 0) &&
               (parse_int < k_max_break)) {
      struct debug_breakpoint* p_breakpoint = &p_debug->breakpoints[parse_int];
      if (p_breakpoint->is_in_use) {
        p_breakpoint->is_enabled = 0;
      }
    } else if (!strcmp(p_command, "eval") && (p_param_1_str != NULL)) {
      int64_t expression_ret;
      struct expression_struct* p_expression = expression_create(
          debug_get_read_variable_function,
          debug_get_write_variable_function,
          p_debug);
      (void) expression_parse(p_expression, p_param_1_str);
      expression_ret = expression_execute(p_expression);
      expression_destroy(p_expression);
      (void) printf("result: %"PRId64" (0x%"PRIx64")\n",
                    expression_ret,
                    expression_ret);
    } else if (!strcmp(p_command, "writem")) {
      uint32_t sequence_len;
      uint32_t i_seq;
      uint16_t addr_start;
      if (num_strings < 3) {
        break;
      }
      sequence_len = (num_strings - 2);
      p_param_str = util_string_list_get_string(p_command_strings, 1);
      addr_start = (uint16_t) util_parse_u64(p_param_str, 1);
      for (i_seq = 0; i_seq < sequence_len; ++i_seq) {
        p_param_str =
            util_string_list_get_string(p_command_strings, (i_seq + 2));
        parse_int = (int32_t) util_parse_u64(p_param_str, 1);
        bbc_memory_write(p_bbc,
                         (uint16_t) (addr_start + i_seq),
                         (uint8_t) parse_int);
      }
    } else if (!strcmp(p_command, "find")) {
      uint32_t sequence_len;
      uint32_t addr_start;
      uint32_t addr_len;
      uint32_t i_addr;
      uint32_t i_seq;
      if (num_strings < 4) {
        break;
      }
      sequence_len = (num_strings - 3);
      p_param_str = util_string_list_get_string(p_command_strings, 1);
      parse_int = (int32_t) util_parse_u64(p_param_str, 1);
      if ((parse_int < 0) || (parse_int > 0xFFFF)) {
        break;
      }
      addr_start = (uint32_t) parse_int;
      p_param_str = util_string_list_get_string(p_command_strings, 2);
      parse_int = (int32_t) util_parse_u64(p_param_str, 1);
      if ((parse_int < 0) || (parse_int > 0xFFFF)) {
        break;
      }
      addr_len = (uint32_t) parse_int;
      /* Not efficiently coded. Terrible in fact. */
      for (i_addr = 0; i_addr < addr_len; ++i_addr) {
        uint16_t addr = (uint16_t) (addr_start + i_addr);
        for (i_seq = 0; i_seq < sequence_len; ++i_seq) {
          uint8_t expect;
          uint8_t actual;
          uint16_t seq_addr = (uint16_t) (addr + i_seq);
          p_param_str = util_string_list_get_string(p_command_strings,
                                                    (i_seq + 3));
          expect = (uint8_t) util_parse_u64(p_param_str, 1);
          actual = p_mem_read[seq_addr];
          if (actual != expect) {
            break;
          }
        }
        if (i_seq == sequence_len) {
          (void) printf("sequence found at %.4X\n", addr);
        }
      }
    } else if (!strcmp(p_command, "inv") &&
               (parse_int >= 0) &&
               (parse_int < 65536)) {
      bbc_memory_write(p_bbc, parse_int, p_mem_read[parse_int]);
    } else if (!strcmp(p_command, "loadmem") &&
               (parse_hex_int2 >= 0) &&
               (parse_hex_int2 < 65536)) {
      debug_load_raw(p_debug, p_param_1_str, parse_hex_int2);
    } else if (!strcmp(p_command, "savemem") &&
               (parse_hex_int2 >= 0) &&
               (parse_hex_int2 < 65536) &&
               (parse_hex_int3 >= 0) &&
               (parse_hex_int3 < 65536)) {
      debug_save_raw(p_debug, p_param_1_str, parse_hex_int2, parse_hex_int3);
    } else if (!strcmp(p_command, "ss")) {
      state_save(p_bbc, p_param_1_str);
    } else if (!strcmp(p_command, "d")) {
      if (parse_hex_int == -1) {
        parse_hex_int = p_debug->reg_pc;
      }
      parse_addr = debug_disass(p_debug, p_cpu_driver, parse_hex_int);
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
      struct timing_struct* p_timing = p_debug->p_timing;
      uint64_t ticks = timing_get_total_timer_ticks(p_timing);
      uint64_t countdown = timing_get_countdown(p_timing);
      uint64_t cycles = state_6502_get_cycles(p_state_6502);
      debug_print_flags_buf(&flags_buf[0], p_debug->reg_flags);
      debug_print_registers(p_debug->reg_a,
                            p_debug->reg_x,
                            p_debug->reg_y,
                            p_debug->reg_s,
                            flags_buf,
                            p_debug->reg_pc,
                            cycles,
                            ticks,
                            countdown);
    } else if (!strcmp(p_command, "ddrive") &&
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
    } else if (!strcmp(p_command, "dtrack") && (parse_int >= 0)) {
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
    } else if (!strcmp(p_command, "drsec") && (parse_int >= 0)) {
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
    } else if (!strcmp(p_command, "dset")) {
      disc_tool_fill_fm_data(p_tool, parse_hex_int);
    } else if (!strcmp(p_command, "dpos")) {
      if (parse_int >= 0) {
        disc_tool_set_byte_pos(p_tool, parse_int);
      } else {
        (void) printf("dpos is %d\n", disc_tool_get_byte_pos(p_tool));
      }
    } else if (!strcmp(p_command, "drfm")) {
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
    } else if (!strcmp(p_command, "drfmc")) {
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
    } else if (!strcmp(p_command, "dwfmc")) {
      uint8_t data = (uint8_t) parse_hex_int;
      uint8_t clocks = (uint8_t) parse_hex_int2;
      disc_tool_write_fm_data(p_tool, &clocks, &data, 1);
    } else if (!strcmp(p_command, "dwfm") && (num_strings > 1)) {
      uint32_t i_seq;
      uint32_t sequence_len = (num_strings - 1);
      if (sequence_len > 64) {
        sequence_len = 64;
      }
      for (i_seq = 0; i_seq < sequence_len; ++i_seq) {
        p_param_str =
            util_string_list_get_string(p_command_strings, (i_seq + 1));
        disc_data[i_seq] = (uint8_t) util_parse_u64(p_param_str, 1);
      }
      disc_tool_write_fm_data(p_tool, NULL, &disc_data[0], sequence_len);
    } else if (!strcmp(p_command, "keydown")) {
      struct keyboard_struct* p_keyboard = bbc_get_keyboard(p_bbc);
      keyboard_system_key_pressed(p_keyboard, (uint8_t) parse_int);
    } else if (!strcmp(p_command, "keyup")) {
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
  "enable <b>         : enable breakpoint <b>\n"
  "disable <b>        : disable breakpoint <b>\n"
  "m <a>              : show memory at <a>\n"
  "writem <a> <v>     : write <v> to memory address <a>\n"
  "find <a> <l> ...   : find a byte sequence, starting at <a>, length <l>\n"
  "loadmem <f> <a>    : load memory to <a> from raw file <f>\n"
  "savemem <f> <a> <l>: save memory from <a>, length <l> to raw file <f>\n"
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
  "eval <expr>        : evaluate expression, e.g. 'a=7' to set A\n"
  "bc <b> <count>     : run until breakpoint <b> hits <count> times\n"
  "stats              : toggle stats collection (default: off)\n"
  "ds                 : dump stats collected\n"
  "cs                 : clear stats collected\n"
  "t                  : trap into gdb\n"
  "breakat <c>        : break at <c> cycles\n"
  "keydown <k>        : simulate key press <k>\n"
  "keyup <k>          : simulate key release <k>\n"
  "ss <f>             : save state to BEM file <f> (deprecated)\n"
  "fast               : toggle fast mode on/off\n"
  "seek <s>           : seek a replay file to <s> seconds\n"
  "bail               : exit emulator with failure code\n"
  );
    } else {
      (void) printf("???\n");
    }
    state_6502_set_registers(p_state_6502,
                             p_debug->reg_a,
                             p_debug->reg_x,
                             p_debug->reg_y,
                             p_debug->reg_s,
                             p_debug->reg_flags,
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

static void
debug_sub_instruction_callback(void* p) {
  /* This is a timer callback. */
  struct debug_struct* p_debug = (struct debug_struct*) p;
  p_debug->sub_instruction_tick++;
  (void) timing_set_timer_value(p_debug->p_timing,
                                p_debug->timer_id_sub_instruction,
                                1);
  (void) debug_callback_common(p_debug, NULL, 0);
}

void*
debug_callback(struct cpu_driver* p_cpu_driver, int do_irq) {
  struct debug_struct* p_debug = p_cpu_driver->abi.p_debug_object;
  p_debug->sub_instruction_tick = 0;
  return debug_callback_common(p_debug, p_cpu_driver, do_irq);
}

struct debug_struct*
debug_create(struct bbc_struct* p_bbc,
             int debug_active,
             struct bbc_options* p_options) {
  uint32_t i;
  struct debug_struct* p_debug;
  struct timing_struct* p_timing = bbc_get_timing(p_bbc);

  assert(s_p_debug == NULL);

  p_debug = util_mallocz(sizeof(struct debug_struct));
  /* NOTE: using this singleton pattern for now so we can use qsort().
   * qsort_r() is a minor porting headache due to differing signatures.
   */
  s_p_debug = p_debug;

  p_debug->p_bbc = p_bbc;
  p_debug->p_state_6502 = bbc_get_6502(p_bbc);
  p_debug->p_mem_read = bbc_get_mem_read(p_bbc);
  p_debug->p_timing = p_timing;
  p_debug->p_video = bbc_get_video(p_bbc);
  p_debug->p_render = bbc_get_render(p_bbc);
  p_debug->p_system_via = bbc_get_sysvia(p_bbc);
  p_debug->p_user_via = bbc_get_uservia(p_bbc);
  p_debug->debug_active = debug_active;
  p_debug->debug_running = bbc_get_run_flag(p_bbc);
  p_debug->debug_running_print = bbc_get_print_flag(p_bbc);
  p_debug->next_or_finish_stop_addr = -1;
  p_debug->p_tool = disc_tool_create();
  p_debug->p_command_strings = util_string_list_alloc();
  p_debug->p_pending_commands = util_string_list_alloc();
  p_debug->breakpoint_continue = -1;

  p_debug->p_temp_storage_buf = util_buffer_create();
  util_buffer_setup_internal(p_debug->p_temp_storage_buf);

  for (i = 0; i < k_max_break; ++i) {
    debug_clear_breakpoint(p_debug, i);
  }

  for (i = 0; i < k_6502_addr_space_size; ++i) {
    p_debug->warn_at_addr_count[i] = 10;
  }

  p_debug->timer_id_debug = timing_register_timer(p_timing,
                                                  "debug_timer",
                                                  debug_timer_callback,
                                                  p_debug);
  p_debug->timer_id_sub_instruction = timing_register_timer(
      p_timing,
      "debug_sub_instruction",
      debug_sub_instruction_callback,
      p_debug);

  if (util_has_option(p_options->p_opt_flags, "debug:sub-instruction")) {
    debug_make_sub_instruction_active(p_debug);
  }
  p_debug->opt_is_print_ticks = util_has_option(p_options->p_opt_flags,
                                                "debug:print-ticks");

  os_terminal_set_ctrl_c_callback(debug_interrupt_callback);

  return p_debug;
}

void
debug_set_commands(struct debug_struct* p_debug, const char* p_commands) {
  util_string_split(p_debug->p_pending_commands, p_commands, ';', '\'');
  debug_interrupt_callback();
}
