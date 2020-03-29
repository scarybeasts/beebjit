- OVERVIEW
Currently, beebjit builds on Linux.
(The early Windows port is currently cobbled together in a simple manner by
cross-compiling on Linux. See build_win_opt.sh.)


- LINUX
To build beebjit and check all is well via the tests, simply run the script:
./build.sh

At the end of the compile and test run, it should print:
All is well!

And you will be left with the debug build of beebjit. This includes asserts and
debug info and is built without optimizations. So it will be slower than the
optimized build but should still move along at a decent clip.

To directly just build the debug or optimized versions of beebjit, use:
./build_dbg.sh
./build_opt.sh

Building beebjit doesn't have many dependencies. You'll need gcc and standard
system headers (including X11 and ALSA) but nothing exotic.
