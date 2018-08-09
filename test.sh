#!/bin/sh

gcc -Wall -g -o 6502jit \
    main.c bbc.c jit.c opdefs.c x.c debug.c util.c \
    -lX11 -lXext -lpthread
gcc -Wall -g -o make_test_rom make_test_rom.c
./make_test_rom

./6502jit -o test.rom -l ''
./6502jit -o test.rom -l '' -d -r

