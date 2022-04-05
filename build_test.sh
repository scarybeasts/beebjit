#!/bin/sh
set -e

gcc -Wall -W -Werror -g -o make_test_rom make_test_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
./make_test_rom

gcc -Wall -W -Werror -g -o make_timing_rom make_timing_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
./make_timing_rom

gcc -Wall -W -Werror -g -o make_master_rom make_master_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
./make_master_rom

gcc -Wall -W -Werror -g -o make_8271_rom make_8271_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
./make_8271_rom

gcc -Wall -W -Werror -g -o make_perf_rom make_perf_rom.c \
    util.c defs_6502.c emit_6502.c test_helper.c
./make_perf_rom
