#!/bin/sh

gcc -o 6502jit main.c
gcc -o make_test_rom make_test_rom.c
./make_test_rom

./6502jit test.rom

