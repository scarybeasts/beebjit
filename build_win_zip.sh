#!/bin/sh
rm -rf win/
mkdir win/
./build_win_opt.sh

cp beebjit.exe win/
cp -r roms/ win/
cp COPYING CREDITS EXAMPLES GETTING_STARTED KEYS README win/
cp test/perf/clocksp.ssd win/
cp test/games/Disc108-FroggerRSCB.ssd win/
cp test/demos/EvilIn11.ssd win/
