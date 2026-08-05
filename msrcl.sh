# must compile with -O3 not -Ofast so NaN support works
clang-18 -DARMOS -DNDEBUG -Wno-psabi -I . -x c++ armos.cxx arm64.cxx -o armoscl -O3 -static -fsigned-char -Wno-format -std=c++14 -lm -lstdc++
# cp armos /mnt/c/users/david/onedrive/armos/bin
