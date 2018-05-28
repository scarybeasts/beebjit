#!/bin/sh

gcc -Wall -g -o 6502jit main.c jit.c x.c -lX11 -lpthread
gcc -Wall -g -o make_test_rom make_test_rom.c
./make_test_rom

./6502jit -o test.rom -l ''

