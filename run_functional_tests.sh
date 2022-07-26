#!/bin/sh
set -e

echo 'Checking Nightshade protection.'
# This injects CH."NIGHTSH" into the keyboard buffer and expects execution to
# arrive at $0E00 in order to pass by exiting.
./beebjit -0 test/misc/protection.ssd \
    -headless -fast -accurate -debug \
    -commands 'breakat 1000000;c;writem 03e0 43 48 2e 22 42 2e 4e 49 47 48 54 53 48 22 0d;writem 02e1 ef;b e00;c;q'
