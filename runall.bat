@echo off
setlocal

if "%1" == "" (set _runcmd=armos -h:100 )
if "%1" == "nested" (set _runcmd=armosg -h:160 bin\armoscl -h:100 )
if "%1" == "pi5" (set _runcmd=armos -h:160 pi5\bin\armos -h:100 )
if "%1" == "pi5cl" (set _runcmd=armos -h:160 pi5\bin\armoscl -h:100 )
if "%1" == "rvos" (set _runcmd=..\rvos\rvos -h:160 ..\rvos\linux\armos -h:100 )
if "%1" == "armosg" (set _runcmd=armosg -h:160 bin\armoscl -h:100 )
if "%1" == "armoscl" (set _runcmd=armoscl -h:100 )

set _testfolder=.\
rem set _testfolder=pi5\

set outputfile=test_armos.txt
echo %date% %time% >%outputfile%

set _folderlist=bin0 bin1 bin2 bin3 binfast
set _applist=tcmp t e printint sieve simple tmuldiv tpi ts ttt tarray tbits trw ^
             tmmap tstr fileops ttime tm glob tap tsimplef tf td terrno ^
             t_setjmp tex mm tao pis ttypes nantst sleeptm tatomic lenum tregex trename
set _optlist=6 8 a d 3 i I m o r x

( for %%f in (%_folderlist%) do ( call :folderRun %%f ) )

set _rustfolders=bin0 bin1 bin2 bin3

( for %%f in (%_rustfolders%) do ( call :rustfolder %%f ) )

echo test ntvao
echo test ntvao >>%outputfile%
%_runcmd% bin\ntvao -c ttt1.hex >>%outputfile%

echo test ntvaocl
echo test ntvaocl >>%outputfile%
%_runcmd% bin\ntvaocl -c ttt1.hex >>%outputfile%

echo test ntvcm
echo test ntvcm >>%outputfile%
%_runcmd% bin\ntvcm tttcpm.com >>%outputfile%

echo test ntvcmcl
echo test ntvcmcl >>%outputfile%
%_runcmd% bin\ntvcmcl tttcpm.com >>%outputfile%

echo test ntvdm
echo test ntvdm >>%outputfile%
%_runcmd% bin\ntvdm tttmasm.exe 1 >>%outputfile%

echo test ntvdmcl
echo test ntvdmcl >>%outputfile%
%_runcmd% bin\ntvdmcl tttmasm.exe 1 >>%outputfile%

echo test rvos
echo test rvos >>%outputfile%
%_runcmd% bin\rvos ttt.elf 1 >>%outputfile%

echo test rvoscl
echo test rvoscl >>%outputfile%
%_runcmd% bin\rvoscl ttt.elf 1 >>%outputfile%

echo test armos
echo test armos >>%outputfile%
%_runcmd% bin\armos ttt_armu 1 >>%outputfile%

echo test armoscl
echo test armoscl >>%outputfile%
%_runcmd% bin\armoscl ttt_armu 1 >>%outputfile%

echo %date% %time% >>%outputfile%
diff baseline_%outputfile% %outputfile%

goto :allDone

:folderRun

( for %%a in (%_applist%) do ( call :appRun c_tests\clang%%f\%%a ) )
( for %%a in (%_applist%) do ( call :appRun c_tests\%%f\%%a ) )

echo test c_tests\%~1\an
echo test c_tests\%~1\an >>%outputfile%
call :anTest c_tests\%~1
echo test c_tests\clang%~1\an
echo test c_tests\clang%~1\an >>%outputfile%
call :anTest c_tests\clang%~1

echo test c_tests\%~1\ba
echo test c_tests\%~1\ba >>%outputfile%
call :baTest c_tests\%~1
echo test c_tests\clang%~1\ba
echo test c_tests\clang%~1\ba >>%outputfile%
call :baTest c_tests\clang%~1

exit /b 0

:appRun
echo test %~1
echo test %~1 >>%outputfile%
%_runcmd% %_testfolder%%~1 >>%outputfile%
exit /b /o

:rustRun
echo test %~1
echo test %~1 >>%outputfile%
%_runcmd% %_testfolder%%~1 >>%outputfile%
exit /b /o

:rustfolder
set _rustlist=e ttt fileops ato tap real tphi mysort tmm
( for %%a in (%_rustlist%) do ( call :rustRun rust_tests\%%f\%%a ) )
exit /b /o

:anTest
%_runcmd% %~1\an david lee >>%outputfile%
exit /b /o

:baTest
%_runcmd% %~1\ba tp.bas >>%outputfile%
( for %%o in (%_optlist%) do ( %_runcmd% %~1\ba -a:%%o -x tp.bas >>%outputfile% ) )
exit /b /o

:allDone


