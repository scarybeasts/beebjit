#!/bin/sh
set -e

./run_unit_tests.sh
./run_scripting_tests.sh
./run_integration_tests.sh
./run_functional_tests.sh

echo 'All is well!'
