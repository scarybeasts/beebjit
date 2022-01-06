#!/bin/sh
set -e

BEEBJIT_PLATFORM=`uname -m`
BEEBJIT_CLANG_EXTRA=""
if [ $BEEBJIT_PLATFORM = "x86_64" ]
then
  BEEBJIT_CLANG_EXTRA="-Wl,-pagezero_size,0x00400000 -fno-pie"
fi

clang -Wall -W -Werror \
      -Wno-unknown-warning-option -Wno-address-of-packed-member \
      -Wa,--noexecstack \
      -framework Cocoa -framework Audiotoolbox \
      $BEEBJIT_CLANG_EXTRA \
      -g -o beebjit \
      asm/asm_abi.c asm/asm_tables.c asm/asm_util.c \
      asm/asm_common.c asm/asm_common.S \
      asm/asm_inturbo.c asm/asm_inturbo.S \
      asm/asm_jit.c asm/asm_jit.S \
      os.c \
      os_window_macos.m \
      main.c config.c bbc.c defs_6502.c state.c video.c via.c \
      emit_6502.c interp.c inturbo.c state_6502.c sound.c timing.c \
      jit_compiler.c cpu_driver.c \
      jit_optimizer.c jit_opcode.c keyboard.c \
      teletext.c render.c mc6850.c serial_ula.c \
      log.c test.c adc.c cmos.c joystick.c \
      tape.c tape_csw.c tape_uef.c \
      intel_fdc.c wd_fdc.c \
      disc_drive.c disc.c ibm_disc_format.c disc_tool.c \
      disc_fsd.c disc_hfe.c disc_ssd.c disc_adl.c \
      disc_rfi.c disc_kryo.c disc_scp.c disc_dfi.c \
      debug.c expression.c jit.c \
      util.c util_string.c util_container.c util_compress.c
