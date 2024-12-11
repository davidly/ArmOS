@echo off
cl /DARMOS /nologo armos.cxx arm64.cxx /I. /EHsc /DDEBUG /O2 /Oi /Fa /FAs /Qpar /Zi /link /OPT:REF user32.lib


