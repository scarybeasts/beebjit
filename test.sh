#!/bin/sh
set -e

gcc -O3 -DNDEBUG -Wall -W -Werror -g -o 6502jit \
    main.c bbc.c jit.c defs_6502.c x.c debug.c util.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c intel_fdc.c timing.c \
    asm_x64_abi.c asm_tables.c test.c \
    asm_x64_common.c asm_x64_inturbo.c \
    asm_x64_common.S asm_x64_jit.S asm_x64_inturbo.S \
    -lm -lX11 -lXext -lpthread -lasound

gcc -Wall -W -Werror -g -o 6502jit \
    main.c bbc.c jit.c defs_6502.c x.c debug.c util.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c intel_fdc.c timing.c \
    asm_x64_abi.c asm_tables.c test.c \
    asm_x64_common.c asm_x64_inturbo.c \
    asm_x64_common.S asm_x64_jit.S asm_x64_inturbo.S \
    -lm -lX11 -lXext -lpthread -lasound

gcc -Wall -W -Werror -g -o make_test_rom make_test_rom.c test_helper.c \
    util.c defs_6502.c emit_6502.c
gcc -Wall -W -Werror -g -o make_perf_rom make_perf_rom.c test_helper.c \
    util.c defs_6502.c emit_6502.c
./make_test_rom

echo 'Running JIT, debug.'
./6502jit -os test.rom -expect 434241 -opt jit:self-mod-all -d -r
echo 'Running built-in tests.'
./6502jit -t
echo 'Running JIT, opt.'
./6502jit -os test.rom -expect 434241 -opt jit:self-mod-all
echo 'Running JIT, opt, no-batch-ops.'
./6502jit -os test.rom -expect 434241 -opt jit:self-mod-all,jit:no-batch-ops
echo 'Running interpreter.'
./6502jit -os test.rom -expect 434241 -mode interp
echo 'Running interpreter, debug, print.'
./6502jit -os test.rom -expect 434241 -mode interp -d -r -p >/dev/null
echo 'Running interpreter, slow mode.'
./6502jit -os test.rom -expect 434241 -mode interp -s
echo 'Running inturbo.'
./6502jit -os test.rom -expect 434241 -mode inturbo

echo 'All is well!'
