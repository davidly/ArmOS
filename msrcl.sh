#g++ -DARMOS -Ofast -DNDEBUG -Wno-psabi -fsigned-char -fno-builtin -Wno-format -I . armos.cxx arm64.cxx -o armos -static
clang-18 -DNDEBUG -DARMOS -DNDEBUG -Wno-psabi -I . -x c++ armos.cxx arm64.cxx -o armoscl -O3 -static -fsigned-char -Wno-format -std=c++14 -lm -lstdc++
# cp armos /mnt/c/users/david/onedrive/armos/bin
