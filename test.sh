#!/bin/sh
set -e

gcc -O3 -DNDEBUG -Wall -W -Werror -g -o 6502jit \
    main.c bbc.c jit.c defs_6502.c x.c debug.c util.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c intel_fdc.c timing.c \
    jit_compiler.c cpu_driver.c asm_x64_abi.c asm_tables.c \
    asm_x64_common.c asm_x64_inturbo.c asm_x64_jit.c \
    asm_x64_common.S asm_x64_inturbo.S asm_x64_jit.S \
    -lm -lX11 -lXext -lpthread -lasound

gcc -Wall -W -Werror -g -o 6502jit \
    main.c bbc.c jit.c defs_6502.c x.c debug.c util.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c intel_fdc.c timing.c \
    jit_compiler.c cpu_driver.c asm_x64_abi.c asm_tables.c \
    asm_x64_common.c asm_x64_inturbo.c asm_x64_jit.c \
    asm_x64_common.S asm_x64_inturbo.S asm_x64_jit.S \
    -lm -lX11 -lXext -lpthread -lasound

gcc -Wall -W -Werror -g -o make_test_rom make_test_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
gcc -Wall -W -Werror -g -o make_timing_rom make_timing_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
gcc -Wall -W -Werror -g -o make_perf_rom make_perf_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
./make_test_rom
./make_timing_rom

echo 'Running built-in tests.'
#./6502jit -t
#echo 'Running test.rom, JIT, debug.'
#./6502jit -os test.rom -expect 434241 -mode jit -opt jit:self-mod-all -d -r
#echo 'Running test.rom, JIT, opt.'
#./6502jit -os test.rom -expect 434241 -mode jit -opt jit:self-mod-all
#echo 'Running test.rom, JIT, opt, no-batch-ops.'
#./6502jit -os test.rom -expect 434241 -mode jit \
#          -opt jit:self-mod-all,jit:no-batch-ops
echo 'Running test.rom, interpreter.'
./6502jit -os test.rom -expect 434241 -mode interp
echo 'Running test.rom, interpreter, debug, print.'
./6502jit -os test.rom -expect 434241 -mode interp -d -r -p >/dev/null
echo 'Running test.rom, interpreter, slow mode.'
./6502jit -os test.rom -expect 434241 -mode interp -s
echo 'Running test.rom, inturbo.'
./6502jit -os test.rom -expect 434241 -mode inturbo
echo 'Running test.rom, inturbo, debug.'
./6502jit -os test.rom -expect 434241 -mode inturbo -d -r
echo 'Running test.rom, inturbo, accurate.'
./6502jit -os test.rom -expect 434241 -mode inturbo -a

echo 'Running timing.rom, interpreter.'
./6502jit -os timing.rom -expect C0C1C2 -mode interp -a
echo 'Running timing.rom, inturbo.'
./6502jit -os timing.rom -expect C0C1C2 -mode inturbo -a

echo 'All is well!'
