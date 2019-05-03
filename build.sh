#!/bin/bash
#compiler="x86_64-w64-mingw32-gcc-7.3-win32"
compiler=gcc

#flags="-D_WIN64 -Werror"
flags="-std=c99 -ggdb"
includes="-Iinclude/"
libs=""
sources="src/server.c"
#linker="-w -Wl,-subsystem,windows -lmingw32"
linker=""

$compiler $flags $includes $libs $sources $linker -o bin/server
$compiler $flags $includes $libs src/client.c $linker -o bin/client
