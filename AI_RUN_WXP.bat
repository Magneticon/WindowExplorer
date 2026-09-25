@echo off
setlocal

set PROGNAME=WindowExplorer
set REPONAME=WindowExplorer
set OUTFILE=WindowExplorer_Out.txt

set ROOT=R:\GPT\CODEX\CODEX\GIT\%REPONAME%\
set OUT=%ROOT%%OUTFILE%

echo %OUTFILE% >"%OUT%"
echo ============================================================================>>"%OUT%"
echo.>>"%OUT%"
echo Hello world! >>"%OUT%"
echo.>>"%OUT%"

rem --------------------------------------------------
rem DO NOT CHANGE BELLOW:

set ARCH=x64
set DBG=G:\CODEX\GIT\CONTROL\DBGRun\%ARCH%\DBGRun.exe
set EXE=%ROOT%bin\Release\WXP\%ARCH%\%PROGNAME%.exe
set DBGOUT=%ROOT%AI_RUN_LOG_WXP.txt

if exist "%DBGOUT%" del "%DBGOUT%"

"%DBG%" "%DBGOUT%" "%EXE%" >>"%OUT%" 2>&1

endlocal