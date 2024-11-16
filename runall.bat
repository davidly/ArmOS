@echo off
setlocal

rem if "%1" == "" (set _runcmd=armos -h:100) else (set _runcmd=armos -h:160 bin\armos -h:100 )
if "%1" == "" (set _runcmd=armos -h:100) else (set _runcmd=rvos -m:160 ..\rvos\linux\armos.elf -h:100)

set outputfile=test_armos.txt
echo %date% %time% >%outputfile%

set _folderlist=bin0 bin1 bin2 bin3 binfast
set _applist=tcmp e printint sieve simple tmuldiv tpi ts ttt tarray tbits trw ^
             tmmap tstr fileops ttime tm glob tap tsimplef tf td terrno ^
             t_setjmp tex

( for %%f in (%_folderlist%) do ( call :folderRun %%f ) )

set _rustfolders=bin0 bin1 bin2 bin3

( for %%f in (%_rustfolders%) do ( call :rustfolder %%f ) )

echo test an
echo test an >>%outputfile%
%_runcmd% c_tests\bin0\an david lee >>%outputfile%
%_runcmd% c_tests\bin1\an david lee >>%outputfile%
%_runcmd% c_tests\bin2\an david lee >>%outputfile%
%_runcmd% c_tests\bin3\an david lee >>%outputfile%

echo test ba
echo test ba >>%outputfile%
%_runcmd% c_tests\bin0\ba tp.bas >>%outputfile%
%_runcmd% c_tests\bin1\ba tp.bas >>%outputfile%
%_runcmd% c_tests\bin2\ba tp.bas >>%outputfile%
%_runcmd% c_tests\bin3\ba tp.bas >>%outputfile%

echo test ntvao
echo test ntvao >>%outputfile%
%_runcmd% bin\ntvao -c ttt1.hex >>%outputfile%

echo test ntvcm
echo test ntvcm >>%outputfile%
%_runcmd% bin\ntvcm tttcpm.com >>%outputfile%

echo test ntvdm
echo test ntvdm >>%outputfile%
%_runcmd% bin\ntvdm tttmasm.exe 1 >>%outputfile%

echo test rvos
echo test rvos >>%outputfile%
%_runcmd% bin\rvos ttt.elf 1 >>%outputfile%

echo test armos
echo test armos >>%outputfile%
%_runcmd% bin\armos ttt_arm64 1 >>%outputfile%

echo %date% %time% >>%outputfile%
diff baseline_%outputfile% %outputfile%

goto :allDone

:folderRun

( for %%a in (%_applist%) do ( call :appRun c_tests\%%f\%%a ) )

exit /b 0

:appRun

echo test %~1
echo test %~1 >>%outputfile%
%_runcmd% %~1 >>%outputfile%

exit /b /o

:rustRun

echo test %~1
echo test %~1 >>%outputfile%
%_runcmd% %~1 >>%outputfile%

exit /b /o

:rustfolder

set _rustlist=e ttt fileops ato tap real tphi mysort
( for %%a in (%_rustlist%) do ( call :rustRun rust_tests\%%f\%%a ) )

exit /b /o

:allDone


