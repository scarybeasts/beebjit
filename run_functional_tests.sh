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
    -mode interp \
    -headless -fast -accurate -debug \
    -autoboot \
    -commands "b expr 'addr==0xfcd0 && is_write && a!=0' commands 'bail';b expr 'addr==0xfcd0 && is_write && a==0' commands 'q';c"

echo 'Checking RVI rendering.'
# This checks the framebuffer looks as expected, once the Bitshifters RVI
# technique is loaded and running.
# Test it with both interp and then JIT.
./beebjit -0 test/display/raster-c.ssd \
    -mode interp \
    -autoboot \
    -fast -accurate -debug \
    -opt video:always-render \
    -commands "breakat 11000000;c;eval '(frame_buffer_crc32==0x2c23c1b6)||bail';q"
./beebjit -0 test/display/raster-c.ssd \
    -mode jit \
    -autoboot \
    -fast -accurate -debug \
    -opt video:always-render \
    -commands "breakat 11000000;c;eval '(frame_buffer_crc32==0x2c23c1b6)||bail';q"

# This checks the framebuffer looks as expected, in an RVI test case that uses
# teletext output to implement pre-line blanking.
./beebjit -0 test/display/rvi-working.ssd \
    -mode interp \
    -autoboot \
    -debug -fast -accurate \
    -opt video:always-render \
    -commands "breakat 6500000;c;b expr 'render_y == 620';c;eval '(frame_buffer_crc32==0x16d251fe)||bail';q"

# This checks the display accuracy of a gnarly MODE1 + MODE7 test case that
# hits corner cases with 2MHz teletext operation, non-rounded teletext
# rendering, teletext line tracking, and more.
./beebjit -0 test/display/mode1+7.ssd \
    -mode interp \
    -debug -fast -accurate \
    -opt video:always-render \
    -commands "breakat 1000000;c;writem 03e0 43 48 2e 22 4d 4f 44 45 31 2f 37 22 0d;writem 02e1 ef;breakat 25000000;c;b expr 'render_y == 620';c;eval '(frame_buffer_crc32==0x646f907a)||bail';q"

# This checks some teletext state machine corner cases.
echo 'Checking teletext rendering.'
./beebjit -0 test/display/teletest_v1.ssd \
    -mode jit \
    -debug -fast -accurate \
    -opt video:always-render \
    -commands "breakat 1000000;c;writem 03e0 43 48 2e 22 54 45 4c 45 54 53 54 22 0d;writem 02e1 ef;breakat 2100000;c;b expr 'render_y == 620';c;eval '(frame_buffer_crc32==0x314d2162)||bail';q"

# This checks some 6845 end-of-frame logic that can render an unexpected
# blank scanline.
echo 'Checking unexpected scanline.'
./beebjit -0 test/display/mode7-75.ssd \
    -mode jit \
    -autoboot \
    -debug -fast -accurate \
    -opt video:always-render \
    -commands "breakat 9000000;c;b expr 'render_y == 621';c;eval '(frame_buffer_crc32==0xe5a2ca70)||bail';q"

# This checks the simple NuLA support.
echo 'Checking NuLA palette.'
./beebjit -0 test/games/Disc108-FroggerRSCB.ssd \
    -mode jit \
    -nula \
    -autoboot \
    -debug -fast -accurate \
    -opt video:always-render \
    -commands "breakat 22000000;c;keydown 90;breakat 24000000;c;b expr 'render_y == 600';c;eval '(frame_buffer_crc32==0x46e9a111)||bail';q"

# This checks a replay of a 100% Nightworld run.
# It breaks at the time the game is writing the "G" of "GAME END".
./beebjit -0 test/games/Disc012-Nightworld.ssd \
    -replay test/caps/nw16.cap \
    -fast -accurate -mode jit \
    -commands "breakat 2110319378;c;eval '(pc==0xCFB7)||bail';eval '(a==0xE8)||bail';q"

echo 'Functional tests OK.'
