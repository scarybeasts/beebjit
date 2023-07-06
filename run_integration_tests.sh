#!/bin/sh
set -e

echo 'Running test.rom, JIT, fast.'
./beebjit -os test.rom -swram f -test-map -expect 434241 \
    -mode jit -fast
echo 'Running test.rom, JIT, fast, debug.'
./beebjit -os test.rom -swram f -test-map -expect 434241 \
    -mode jit -fast -debug -run
echo 'Running test.rom, JIT, fast, accurate.'
./beebjit -os test.rom -swram f -test-map -expect 434241 \
    -mode jit -fast -accurate
echo 'Running test.rom, interpreter, fast.'
./beebjit -os test.rom -swram f -test-map -expect 434241 -mode interp -fast
echo 'Running test.rom, interpreter, fast, debug, print.'
./beebjit -os test.rom -swram f -test-map -expect 434241 -mode interp -fast \
    -debug -run -print >/dev/null
echo 'Running test.rom, interpreter, slow.'
./beebjit -os test.rom -swram f -test-map -expect 434241 -mode interp
echo 'Running test.rom, inturbo, fast.'
./beebjit -os test.rom -swram f -test-map -expect 434241 -mode inturbo -fast
echo 'Running test.rom, inturbo, fast, debug.'
./beebjit -os test.rom -swram f -test-map -expect 434241 \
    -mode inturbo -fast -debug -run
echo 'Running test.rom, inturbo, fast, accurate.'
./beebjit -os test.rom -swram f -test-map -expect 434241 \
    -mode inturbo -fast -accurate

echo 'Running timing.rom, interpreter, slow.'
./beebjit -os timing.rom -test-map -expect 434241 -mode interp
echo 'Running timing.rom, interpreter, fast.'
./beebjit -os timing.rom -test-map -expect 434241 -mode interp -fast -accurate
echo 'Running timing.rom, inturbo, fast.'
./beebjit -os timing.rom -test-map -expect 434241 -mode inturbo -fast -accurate
echo 'Running timing.rom, jit, fast.'
./beebjit -os timing.rom -test-map -expect 434241 -mode jit -fast -accurate
echo 'Running timing.rom, jit, fast, debug.'
./beebjit -os timing.rom -test-map -expect 434241 -mode jit -fast -accurate \
    -debug -run

echo 'Running master.rom, interpreter.'
./beebjit -master -os master.rom -test-map -expect 434241 -mode interp

echo 'Running 8271.rom, interpreter.'
./beebjit -os 8271.rom -0 test/empty/0bytefile.ssd -writeable -test-map \
    -mode interp

echo 'Integration tests OK.'
