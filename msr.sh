# must compile with -O3 not -Ofast so NaN support works
g++ -DARMOS -O3 -DNDEBUG -Wno-psabi -Wno-stringop-overflow -fsigned-char -fno-builtin -Wno-format -I . armos.cxx arm64.cxx -o armos -static
# cp armos /mnt/c/users/david/onedrive/armos/bin
