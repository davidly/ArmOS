#!/bin/bash

for arg in ato e fileops mysort real tap td tphi ttt;
do
    echo $arg
    for optflag in 0 1 2 3;
    do    
        mkdir /mnt/c/users/david/onedrive/armos/rust_tests/bin$optflag 2>/dev/null
        cp bin$optflag/$arg /mnt/c/users/david/onedrive/armos/rust_tests/bin$optflag
    done
done

