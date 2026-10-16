#!/bin/sh
set -e

echo 'Checking debugger PC change.'
# This stops at the RESET entry, a 2 byte LDA #, points PC at a NOP at $2000
# and steps once. The NOP must run, leaving PC at $2001.
./beebjit -headless -debug -mode interp \
    -commands "writem 2000 ea;eval pc=0x2000;s;eval '(pc==0x2001)||bail';q" \
    > /dev/null

# This checks that all 64k of memory saves in one go and loads back.
echo 'Checking savemem / loadmem of 64k.'
TMP_MEM=$(mktemp)
./beebjit -headless -debug \
    -commands "writem 2000 5a;writem 7fff a5;savemem $TMP_MEM 0 10000;q" \
    > /dev/null
SAVEMEM_SIZE=$(wc -c < "$TMP_MEM")
if [ "$SAVEMEM_SIZE" -ne 65536 ]; then
  rm -f "$TMP_MEM"
  echo "FAIL: savemem file size $SAVEMEM_SIZE, expected 65536"
  exit 1
fi
./beebjit -headless -debug \
    -commands "loadmem $TMP_MEM 0;eval '(mem[0x2000]==0x5a)||bail';eval '(mem[0x7fff]==0xa5)||bail';q" \
    > /dev/null
rm -f "$TMP_MEM"

# This checks that a key pressed in the debugger reaches the keyboard before
# the next instruction. At the BASIC prompt the keyboard is in auto-scan, so
# the key shows at once as a CA2 interrupt flag in the system VIA.
echo 'Checking debugger keydown.'
./beebjit -headless -fast -accurate -debug \
    -commands "breakat 2000000;c;keydown 90;eval '(sysvia_r[13]&1)||bail';q" \
    > /dev/null

echo 'Checking debugger initiated IRQs fire correctly.'
TMP_CAP=$(mktemp)
TMP_CAP2=$(mktemp)
./beebjit -mode interp -headless -fast -accurate -debug -capture $TMP_CAP \
    -commands "b ffe0;c;keydown 65;s;s;eval '(pc==0xDC1C)||bail';q" \
    > /dev/null

echo 'Checking replay and capture.'
./beebjit -mode interp -headless -fast -accurate -debug -replay $TMP_CAP \
    -commands "b ffe0;c;s;s;eval '(pc==0xDC1C)||bail';q" \
    > /dev/null
./beebjit -mode interp -headless -fast -accurate \
    -replay $TMP_CAP -capture $TMP_CAP2 \
    -cycles 755000 \
    > /dev/null
./beebjit -mode interp -headless -fast -accurate -debug -replay $TMP_CAP2 \
    -commands "b ffe0;c;s;s;eval '(pc==0xDC1C)||bail';q" \
    > /dev/null

rm -f "$TMP_CAP"
rm -f "$TMP_CAP2"

echo 'Scripting tests OK.'
