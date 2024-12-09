clang-18 -x c++ $1.c -o $1 -O3 -static -fsigned-char -Wno-format -std=c++14 -lm -lstdc++
