#!/bin/sh
set -e

gcc -O3 -DNDEBUG -Wall -W -Werror -g -o 6502jit \
    main.c bbc.c jit.c defs_6502.c debug.c util.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c intel_fdc.c timing.c \
    jit_compiler.c cpu_driver.c asm_x64_abi.c asm_tables.c \
    asm_x64_common.c asm_x64_inturbo.c asm_x64_jit.c \
    asm_x64_common.S asm_x64_inturbo.S asm_x64_jit.S \
    jit_optimizer.c jit_opcode.c keyboard.c os.c \
    -lm -lX11 -lXext -lpthread -lasound

gcc -Wall -W -Werror -g -o 6502jit \
    main.c bbc.c jit.c defs_6502.c debug.c util.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c intel_fdc.c timing.c \
    jit_compiler.c cpu_driver.c asm_x64_abi.c asm_tables.c \
    asm_x64_common.c asm_x64_inturbo.c asm_x64_jit.c \
    asm_x64_common.S asm_x64_inturbo.S asm_x64_jit.S \
    jit_optimizer.c jit_opcode.c keyboard.c os.c \
    -lm -lX11 -lXext -lpthread -lasound

gcc -Wall -W -Werror -g -o make_test_rom make_test_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
gcc -Wall -W -Werror -g -o make_timing_rom make_timing_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
gcc -Wall -W -Werror -g -o make_perf_rom make_perf_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
./make_test_rom
./make_timing_rom

#echo 'Running built-in tests.'
#./6502jit -t
echo 'Running test.rom, JIT, fast.'
./6502jit -os test.rom -expect 434241 -mode jit -f
echo 'Running test.rom, JIT, fast, debug.'
./6502jit -os test.rom -expect 434241 -mode jit -f -d -r
echo 'Running test.rom, JIT, fast, accurate.'
./6502jit -os test.rom -expect 434241 -mode jit -f -a
echo 'Running test.rom, interpreter, fast.'
./6502jit -os test.rom -expect 434241 -mode interp -f
echo 'Running test.rom, interpreter, fast, debug, print.'
./6502jit -os test.rom -expect 434241 -mode interp -f -d -r -p >/dev/null
echo 'Running test.rom, interpreter, slow.'
./6502jit -os test.rom -expect 434241 -mode interp
echo 'Running test.rom, inturbo, fast.'
./6502jit -os test.rom -expect 434241 -mode inturbo -f
echo 'Running test.rom, inturbo, fast, debug.'
./6502jit -os test.rom -expect 434241 -mode inturbo -f -d -r
echo 'Running test.rom, inturbo, fast, accurate.'
./6502jit -os test.rom -expect 434241 -mode inturbo -f -a

echo 'Running timing.rom, interpreter, slow.'
./6502jit -os timing.rom -expect C0C1C2 -mode interp
echo 'Running timing.rom, interpreter, fast.'
./6502jit -os timing.rom -expect C0C1C2 -mode interp -f -a
echo 'Running timing.rom, inturbo, fast.'
./6502jit -os timing.rom -expect C0C1C2 -mode inturbo -f -a
echo 'Running timing.rom, jit, fast.'
./6502jit -os timing.rom -expect C0C1C2 -mode jit -f -a
echo 'Running timing.rom, jit, fast, debug.'
./6502jit -os timing.rom -expect C0C1C2 -mode jit -f -a -d -r

echo 'All is well!'
