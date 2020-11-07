#!/bin/sh

# NOTE: -gdwarf-2 needed for my version of wine to recognize the symbols.

x86_64-w64-mingw32-gcc -Wall -W -Werror \
    -O3 -DNDEBUG -o beebjit.exe \
    main.c config.c bbc.c defs_6502.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c timing.c \
    jit_compiler.c cpu_driver.c asm_x64_abi.c asm_tables.c \
    asm_x64_common.c asm_x64_inturbo.c asm_x64_jit.c \
    asm_x64_common.S asm_x64_inturbo.S asm_x64_jit.S \
    jit_optimizer.c jit_opcode.c keyboard.c \
    teletext.c render.c serial.c log.c test.c tape.c adc.c cmos.c \
    intel_fdc.c wd_fdc.c \
    disc_drive.c disc.c ibm_disc_format.c disc_tool.c \
    disc_fsd.c disc_hfe.c disc_ssd.c disc_adl.c \
    disc_rfi.c disc_kryo.c disc_scp.c \
    debug.c jit.c util.c \
    os.c \
    -lgdi32 -lwinmm
