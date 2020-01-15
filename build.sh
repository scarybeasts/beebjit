#!/bin/sh
set -e

gcc -O3 -DNDEBUG -Wall -W -Werror -g -o beebjit \
    main.c bbc.c jit.c defs_6502.c debug.c util.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c intel_fdc.c timing.c \
    jit_compiler.c cpu_driver.c asm_x64_abi.c asm_tables.c \
    asm_x64_common.c asm_x64_inturbo.c asm_x64_jit.c \
    asm_x64_common.S asm_x64_inturbo.S asm_x64_jit.S \
    jit_optimizer.c jit_opcode.c keyboard.c os.c \
    teletext.c render.c serial.c log.c test.c disc.c \
    -lm -lX11 -lXext -lpthread -lasound

gcc -Wall -W -Werror -g -o beebjit \
    main.c bbc.c jit.c defs_6502.c debug.c util.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c intel_fdc.c timing.c \
    jit_compiler.c cpu_driver.c asm_x64_abi.c asm_tables.c \
    asm_x64_common.c asm_x64_inturbo.c asm_x64_jit.c \
    asm_x64_common.S asm_x64_inturbo.S asm_x64_jit.S \
    jit_optimizer.c jit_opcode.c keyboard.c os.c \
    teletext.c render.c serial.c log.c test.c disc.c \
    -lm -lX11 -lXext -lpthread -lasound

gcc -Wall -W -Werror -g -o make_test_rom make_test_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
gcc -Wall -W -Werror -g -o make_timing_rom make_timing_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
gcc -Wall -W -Werror -g -o make_perf_rom make_perf_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
./make_test_rom
./make_timing_rom

echo 'Running built-in unit tests.'
./beebjit -test
echo 'Running test.rom, JIT, fast.'
./beebjit -os test.rom -expect 434241 -mode jit -fast
echo 'Running test.rom, JIT, fast, debug.'
./beebjit -os test.rom -expect 434241 -mode jit -fast -debug -run
echo 'Running test.rom, JIT, fast, accurate.'
./beebjit -os test.rom -expect 434241 -mode jit -fast -accurate
echo 'Running test.rom, interpreter, fast.'
./beebjit -os test.rom -expect 434241 -mode interp -fast
echo 'Running test.rom, interpreter, fast, debug, print.'
./beebjit -os test.rom -expect 434241 -mode interp -fast -debug -run -print \
    >/dev/null
echo 'Running test.rom, interpreter, slow.'
./beebjit -os test.rom -expect 434241 -mode interp
echo 'Running test.rom, inturbo, fast.'
./beebjit -os test.rom -expect 434241 -mode inturbo -fast
echo 'Running test.rom, inturbo, fast, debug.'
./beebjit -os test.rom -expect 434241 -mode inturbo -fast -debug -run
echo 'Running test.rom, inturbo, fast, accurate.'
./beebjit -os test.rom -expect 434241 -mode inturbo -fast -accurate

echo 'Running timing.rom, interpreter, slow.'
./beebjit -os timing.rom -test-map -expect C0C1C2 -mode interp
echo 'Running timing.rom, interpreter, fast.'
./beebjit -os timing.rom -test-map -expect C0C1C2 -mode interp -fast -accurate
echo 'Running timing.rom, inturbo, fast.'
./beebjit -os timing.rom -test-map -expect C0C1C2 -mode inturbo -fast -accurate
echo 'Running timing.rom, jit, fast.'
./beebjit -os timing.rom -test-map -expect C0C1C2 -mode jit -fast -accurate
echo 'Running timing.rom, jit, fast, debug.'
./beebjit -os timing.rom -test-map -expect C0C1C2 -mode jit -fast -accurate \
    -debug -run

echo 'All is well!'
