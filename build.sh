#!/bin/sh
set -e

./build_opt.sh
./build_dbg.sh

./build_test.sh

./run_tests.sh
