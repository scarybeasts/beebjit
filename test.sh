#!/bin/sh

gcc -Wall -g -o 6502jit \
    main.c bbc.c jit.c opdefs.c x.c debug.c util.c state.c video.c \
    -lX11 -lXext -lpthread
gcc -Wall -g -o make_test_rom make_test_rom.c
./make_test_rom

./6502jit -os test.rom -lang '' || echo 'FAIL! non-debug'
./6502jit -os test.rom -lang '' -d -r || echo 'FAIL! debug'

