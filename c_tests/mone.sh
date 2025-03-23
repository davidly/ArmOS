#!/bin/bash

for optflag in 0 1 2 3 fast;
do
    mkdir bin"$optflag" 2>/dev/null
    mkdir clangbin"$optflag" 2>/dev/null

    _clangbuild="clang-18 -x c++ $1.c -o clangbin"$optflag"/$1 -O"$optflag" -static -Wno-implicit-const-int-float-conversion -fsigned-char -Wno-format -Wno-format-security -std=c++14 -lm -lstdc++"
    _gnubuild="g++ $1.c -o bin"$optflag"/$1 -O"$optflag" -static -fsigned-char -Wno-format -Wno-format-security"

    if [ "$optflag" != "fast" ]; then
        $_clangbuild &
        $_gnubuild &
    else    
        $_clangbuild
        $_gnubuild
    fi
done

echo "Waiting for all processes to complete..."
wait
