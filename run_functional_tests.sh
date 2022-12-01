#!/bin/sh
set -e

echo 'Checking Nightshade protection.'
# This injects CH."NIGHTSH" into the keyboard buffer and expects execution to
# arrive at $0E00 in order to pass by exiting.
./beebjit -0 test/misc/protection.ssd \
    -mode jit \
    -headless -fast -accurate -debug \
    -commands 'breakat 1000000;c;writem 03e0 43 48 2e 22 42 2e 4e 49 47 48 54 53 48 22 0d;writem 02e1 ef;b e00;c;q'

echo 'Checking E00 DFS ROM in sideways RAM.'
# This uses raster-c.ssd for convenience because it has a !BOOT and cleanly
# executes at $1900 if it loads correctly.
./beebjit -swram d -rom d roms/E00DFS090 -0 test/display/raster-c.ssd \
    -mode jit \
    -headless -fast -accurate -debug \
    -autoboot \
    -commands 'b 1900;c;q'

echo 'Checking 6502 instruction timings (with cycle stretch).'
# The test itself writes the number of failures to $FCD0, so breakpoint
# expressions are used to trap and consider the write.
./beebjit -0 test/misc/6502timing1M.ssd \
     -mode jit \
     -headless -fast -accurate -debug \
     -autoboot \
     -commands "b expr 'addr==0xfcd0 && is_write && a!=0' commands 'bail';b expr 'addr==0xfcd0 && is_write && a==0' commands 'q';c"

echo 'Checking 65C12 instruction timings (with cycle stretch).'
./beebjit -0 test/misc/65C12timing1M.ssd \
     -master \
     -headless -fast -accurate -debug \
     -autoboot \
     -commands "b expr 'addr==0xfcd0 && is_write && a!=0' commands 'bail';b expr 'addr==0xfcd0 && is_write && a==0' commands 'q';c"

echo 'Functional tests OK.'
