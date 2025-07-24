@echo off
setlocal

if "%1" == "" (set _runcmd=armos -h:100 )
if "%1" == "nested" (set _runcmd=armosg -h:160 bin\armoscl -h:100 )
if "%1" == "pi5" (set _runcmd=armos -h:160 pi5\bin\armos -h:100 )
if "%1" == "pi5cl" (set _runcmd=armos -h:160 pi5\bin\armoscl -h:100 )
if "%1" == "rvos" (set _runcmd=..\rvos\rvos -h:160 ..\rvos\linux\armos -h:100 )
if "%1" == "armosg" (set _runcmd=armosg -h:160 bin\armoscl -h:100 )
if "%1" == "armoscl" (set _runcmd=armoscl -h:100 )

set outputfile=runall_test.txt
echo %date% %time% >%outputfile%

set _folderlist=bin0 bin1 bin2 bin3 binfast
set _applist=tcmp t e printint sieve simple tmuldiv tpi ts tarray tbits trw ^
             tmmap tstr tdir fileops ttime tm glob tap tsimplef tphi tf ttt td terrno ^
             t_setjmp tex mm tao pis ttypes nantst sleeptm tatomic lenum ^
             tregex trename

( for %%a in (%_applist%) do (
    echo %%a
    ( for %%f in (%_folderlist%) do (
        echo c_tests/%%f/%%a>>%outputfile%
        %_runcmd% c_tests\%%f\%%a >>%outputfile%
        echo c_tests/clang%%f/%%a>>%outputfile%
        %_runcmd% c_tests\clang%%f\%%a >>%outputfile%
    ) )
) )

echo test AN
( for %%f in (%_folderlist%) do (
    echo c_tests/%%f/an david lee>>%outputfile%
    %_runcmd% c_tests\%%f\an david lee >>%outputfile%
    echo c_tests/clang%%f/an david lee>>%outputfile%
    %_runcmd% c_tests\clang%%f\an david lee >>%outputfile%
) )

echo test BA
set _optlist=6 8 a d 3 i I m o r x

( for %%f in (%_folderlist%) do (
    echo c_tests/%%f/ba c_tests/tp.bas>>%outputfile%
    %_runcmd% c_tests\\%%f\ba c_tests\tp.bas >>%outputfile%
    ( for %%o in (%_optlist%) do (
        %_runcmd% c_tests\%%f\ba -a:%%o -x c_tests\tp.bas >>%outputfile%
        %_runcmd% c_tests\clang%%f\ba -a:%%o -x c_tests\tp.bas >>%outputfile%
    ) )
) )

set _rustlist=e ttt fileops ato tap real tphi mysort tmm
set _rustfolders=bin0 bin1 bin2 bin3

( for %%a in (%_rustlist%) do (
    echo %%%a
    ( for %%f in (%_rustfolders%) do (
        echo rust_tests/%%f/%%a>>%outputfile%
        %_runcmd% rust_tests\%%f\%%a >>%outputfile%
    ) )
) )

echo %date% %time% >>%outputfile%
dos2unix %outputfile%
diff baseline_%outputfile% %outputfile%


