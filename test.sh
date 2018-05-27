#!/bin/sh

gcc -g -o 6502jit main.c jit.c
gcc -g -o make_test_rom make_test_rom.c
./make_test_rom

./6502jit -o test.rom -l ''

