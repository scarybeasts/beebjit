#!/bin/sh
set -e

gcc -Wall -W -Werror \
    -Wno-unknown-warning-option -Wno-address-of-packed-member \
    -fno-pie -no-pie \
    -g -o beebjit \
    main.c bbc.c jit.c defs_6502.c debug.c util.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c intel_fdc.c timing.c \
    jit_compiler.c cpu_driver.c asm_x64_abi.c asm_tables.c \
    asm_x64_common.c asm_x64_inturbo.c asm_x64_jit.c \
    asm_x64_common.S asm_x64_inturbo.S asm_x64_jit.S \
    jit_optimizer.c jit_opcode.c keyboard.c os.c \
    teletext.c render.c serial.c log.c test.c disc.c ibm_disc_format.c tape.c \
    disc_fsd.c disc_hfe.c disc_ssd.c \
    -lm -lX11 -lXext -lpthread -lasound
