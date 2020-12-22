#include "asm_defs_registers_arm64.h"

#include "../asm_defs_host.h"
#include "../asm_inturbo_defs.h"
#include "asm_calling_convention_arm64.h"

.file "asm_common_arm64.S"
.section rodata
.text


.globl asm_restore_AXYS_PC_flags
.globl asm_restore_AXYS_PC_flags_END
asm_restore_AXYS_PC_flags:
  ldr REG_SCRATCH1, [REG_CONTEXT, #K_CONTEXT_OFFSET_STATE_6502]
  ldr REG_SCRATCH1_32, [REG_SCRATCH1, #K_STATE_6502_OFFSET_REG_PC]
  movz REG_SCRATCH2, #(K_BBC_MEM_READ_FULL_ADDR >> 16), lsl #16
  movk REG_SCRATCH2, #(K_BBC_MEM_READ_FULL_ADDR & 0xFFFF)
  add REG_6502_PC, REG_SCRATCH1, REG_SCRATCH2
  ret

asm_restore_AXYS_PC_flags_END:
  ret


.globl asm_enter
.globl asm_enter_END
asm_enter:

  # param1, x0 == context
  # param2, x1 == x64 start address
  # param3, x2 == countdown
  # param4, x3 == mem base

  mov REG_CONTEXT, REG_PARAM1

  bl asm_restore_AXYS_PC_flags

  movz REG_INTURBO_CODE, #(K_INTURBO_OPCODES >> 16), lsl #16
  movk REG_INTURBO_CODE, #(K_INTURBO_OPCODES & 0xFFFF)
  movz REG_SCRATCH1, #0

  blr x1

.asm_enter_END:
  ret
  
