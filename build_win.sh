#!/bin/sh

x86_64-w64-mingw32-gcc -Wall -W -Werror \
    -g -o beebjit.exe \
    main.c
