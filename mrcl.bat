@echo off
setlocal
path=c:\program files\microsoft visual studio\2022\community\vc\tools\llvm\x64\bin;%path%

rem compile with -O3 not -Ofast so NaN works
clang++ -DARMOS -DNDEBUG -Wno-psabi -I . -x c++ armos.cxx arm64.cxx -o armoscl.exe -O3 -static -fsigned-char -Wno-format -std=c++14 -Wno-deprecated-declarations -luser32.lib
