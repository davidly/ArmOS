@echo off
setlocal

g++ -Ofast -ggdb -fsigned-char -D ARMOS -D _MSC_VER armos.cxx arm64.cxx -I ../djl -D NDEBUG -o armosg.exe -static


