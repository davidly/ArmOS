#!/bin/bash

for arg in tcmp t e printint sieve simple tmuldiv tpi ts tarray tbits trw tmmap tstr \
           fileops ttime tm glob tap tsimplef tphi tf ttt td terrno t_setjmp tex \
           pis mm sleeptm tatomic lenum tregex an ba;
do
    echo $arg
    for optflag in 0 1 2 3 fast;
    do
        mkdir bin"$optflag" 2>/dev/null
        g++ "$arg".c -o bin"$optflag"/"$arg" -O"$optflag" -static -fsigned-char -Wno-format -Wno-format-security

        mkdir clangbin"$optflag" 2>/dev/null
        clang-18 -x c++ "$arg".c -o clangbin"$optflag"/"$arg" -O"$optflag" -static -fsigned-char -Wno-format -Wno-format-security -std=c++14 -lm -lstdc++
    done
done

