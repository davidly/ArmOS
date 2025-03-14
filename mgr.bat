@echo off
setlocal

rem compile with -O3 not -Ofast so NaN works
g++ -O3 -ggdb -fsigned-char -D ARMOS -D _MSC_VER armos.cxx arm64.cxx -I ../djl -D NDEBUG -o armosg.exe -static


