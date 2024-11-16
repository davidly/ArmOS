#!/bin/bash

_armoscmd="armos"
if [ "$1" != "" ]; then
    _armoscmd="armos -h:200 armos"
fi    

outputfile="linux_test.txt"
date_time=$(date)
echo "$date_time" >$outputfile

for arg in tcmp e printint sieve simple tmuldiv tpi ts tarray tbits trw tmmap tstr \
           fileops ttime tm glob tap tsimplef tphi tf ttt td terrno t_setjmp tex;
do
    echo $arg
    for opt in 0 1 2 3 fast;
    do
        echo c_tests/bin$opt/$arg >>$outputfile
        $_armoscmd c_tests/bin$opt/$arg >>$outputfile
    done
done

echo test AN
for opt in 0 1 2 3 fast;
do
    echo c_tests/bin$opt/an david lee >>$outputfile
    $_armoscmd c_tests/bin$opt/an david lee >>$outputfile
done

echo test BA
for opt in 0 1 2 3 fast;
do
    echo c_tests/bin$opt/ba c_tests/tp.bas >>$outputfile
    $_armoscmd c_tests/bin$opt/ba c_tests/tp.bas >>$outputfile
done

for arg in e ttt fileops ato tap real tphi mysort;
do
    echo $arg
    for opt in 0 1 2 3;
    do
        echo rust_tests/bin$opt/$arg >>$outputfile
        $_armoscmd rust_tests/bin$opt/$arg >>$outputfile
    done
done

echo "$date_time" >>$outputfile
diff baseline_$outputfile $outputfile