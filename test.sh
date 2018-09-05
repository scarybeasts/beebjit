#!/bin/sh
set -e

gcc -Wall -g -o 6502jit \
    main.c bbc.c jit.c opdefs.c x.c debug.c util.c state.c video.c via.c \
    test.c \
    -lX11 -lXext -lpthread
gcc -Wall -g -o make_test_rom make_test_rom.c
./make_test_rom

./6502jit -t
./6502jit -os test.rom -lang '' -opt jit:self-mod-all
./6502jit -os test.rom -lang '' -opt jit:self-mod-all -d -r

echo 'All is well!'
