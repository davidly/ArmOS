#!/bin/bash

for arg in tcmp t e printint sieve simple tmuldiv tpi ts tarray tbits trw tmmap tstr \
           fileops ttime tm glob tap tsimplef tphi tf ttt td terrno t_setjmp tex \
           tprintf pis mm tao ttypes nantst sleeptm tatomic lenum tregex trename an ba;
do
    echo $arg
    for optflag in 0 1 2 3 fast;
    do
        mkdir bin"$optflag" 2>/dev/null
        mkdir clangbin"$optflag" 2>/dev/null

        _clangbuild="clang-18 -x c++ "$arg".c -o clangbin"$optflag"/"$arg" -O"$optflag" -static -Wno-implicit-const-int-float-conversion -fsigned-char -Wno-format -Wno-format-security -std=c++14 -lm -lstdc++"
        _gnubuild="g++ "$arg".c -o bin"$optflag"/"$arg" -O"$optflag" -static -fsigned-char -Wno-format -Wno-format-security"

        if [ "$optflag" != "fast" ]; then
            $_clangbuild &
            $_gnubuild &
        else    
            $_clangbuild
            $_gnubuild
        fi

    done
done

echo "Waiting for all processes to complete..."
wait

