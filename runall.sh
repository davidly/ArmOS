#!/bin/bash

_armoscmd="armos"

if [ "$1" = "nested" ]; then
    _armoscmd="armos -h:200 bin/armos"
elif [ "$1" = "native" ]; then
    _armoscmd=""
elif [ "$1" = "armoscl" ]; then
    _armoscmd="armoscl -h:200"
elif [ "$1" = "m68" ]; then
    _armoscmd="../m68/m68 -h:200 ../m68/armos/armos -h:100"
elif [ "$1" = "sparcos" ]; then
    _armoscmd="../sparcos/sparcos -h:200 ../sparcos/bin/armos-sparc.elf -h:100"
elif [ "$1" = "rvos" ]; then
    _armoscmd="../rvos/rvos -h:200 ../rvos/bin/armos -h:100"
elif [ "$1" = "rvoscl" ]; then
    _armoscmd="../rvos/rvoscl -h:200 ../rvos/bin/armos -h:100"
fi    

outputfile="runall_test.txt"
date_time=$(date)
echo "$date_time" >$outputfile

for arg in tcmp t e printint sieve simple tmuldiv tpi ts tarray tbits trw trw2 tmmap tstr \
           tdir fileops ttime tm glob tap tsimplef tphi tf ttt td terrno t_setjmp tex \
           mm tao pis ttypes nantst sleeptm tatomic lenum tregex trename nqueens;
do
    echo $arg
    for opt in 0 1 2 3 fast;
    do
        echo c_tests/bin$opt/$arg >>$outputfile
        $_armoscmd c_tests/bin$opt/$arg >>$outputfile
        echo c_tests/clangbin$opt/$arg >>$outputfile
        $_armoscmd c_tests/clangbin$opt/$arg >>$outputfile
    done
done

for arg in e_arm sieve_arm tttu_arm
do
    echo $arg
    echo c_tests/$arg >>$outputfile
    $_armoscmd c_tests/$arg >>$outputfile
done

echo test AN
for opt in 0 1 2 3 fast;
do
    echo c_tests/bin$opt/an david lee >>$outputfile
    $_armoscmd c_tests/bin$opt/an david lee >>$outputfile
    echo c_tests/clangbin$opt/an david lee >>$outputfile
    $_armoscmd c_tests/clangbin$opt/an david lee >>$outputfile
done

echo test BA
for opt in 0 1 2 3 fast;
do
    echo c_tests/bin$opt/ba c_tests/tp.bas >>$outputfile
    $_armoscmd c_tests/bin$opt/ba c_tests/tp.bas >>$outputfile
    for codegen in 6 8 a d 3 i I m o r x;
    do
        $_armoscmd c_tests/bin$opt/ba -a:$codegen -x c_tests/tp.bas >>$outputfile
        $_armoscmd c_tests/clangbin$opt/ba -a:$codegen -x c_tests/tp.bas >>$outputfile
    done
done

echo test ff . ff.c
for opt in 0 1 2 3 fast;
do
    echo test c_tests/bin$opt/ff . ff.c >>$outputfile
    $_armoscmd c_tests/bin$opt/ff . ff.c >>$outputfile
    echo test c_tests/clangbin$opt/ff . ff.c >>$outputfile
    $_armoscmd c_tests/clangbin$opt/ff . ff.c >>$outputfile
done

for arg in e ttt fileops ato tap real tphi mysort tmm;
do
    echo $arg
    for opt in 0 1 2 3;
    do
        echo rust_tests/bin$opt/$arg >>$outputfile
        $_armoscmd rust_tests/bin$opt/$arg >>$outputfile
    done
done

date_time=$(date)
echo "$date_time" >>$outputfile
diff baseline_$outputfile $outputfile
