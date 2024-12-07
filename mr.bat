@echo off
cl /DARMOS /W4 /wd4996 /nologo /jumptablerdata armos.cxx arm64.cxx /DNDEBUG /I. /EHsc /Ot /Ox /Ob3 /Fa /FAs /Qpar /Zi /link /OPT:REF user32.lib


