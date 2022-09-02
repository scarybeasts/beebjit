#!/bin/sh
set -e

echo 'Running built-in unit tests.'
./beebjit -test

echo 'Unit tests OK.'
