@echo off
cl /W4 /wd4996 /nologo armos.cxx arm64.cxx /DNDEBUG /I. /EHsc /Ot /Ox /Ob3 /Fa /Qpar /Zi /link /OPT:REF user32.lib

