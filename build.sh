#!/bin/bash
compiler="x86_64-w64-mingw32-gcc-7.3-win32"

flags="-D_WIN64 -Werror"
includes="-Iinclude/"
libs=""
sources="src/server.c"
linker="-w -Wl,-subsystem,windows -lmingw32"

$compiler $flags $includes $libs $sources $linker -o bin/server.exe
