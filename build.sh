#! /bin/sh

# xcompile with for example:
# CFLAGS="-march=mips32 -mno-mips16" CC=mipsel-linux-gcc ./build.sh

# local compile
# CFLAGS='-DMYTERM_DATA="./data/"' ./build.sh

test -z "$CC" && CC=gcc

$CC     $CFLAGS -ffast-math -fomit-frame-pointer -fwhole-program -flto=2 \
        -W -Wall -Wno-unused-parameter -O3 -s \
        `pkg-config --libs --cflags sdl ` -lSDL_image \
        -o myterm myterm.c

