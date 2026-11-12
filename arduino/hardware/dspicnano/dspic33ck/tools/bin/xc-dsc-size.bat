@echo off
REM Shim for one XC-DSC tool: resolve the compiler install, then run the real
REM <this file's name>.exe from it.
REM
REM The tool name comes from this file's OWN name (%~n0), which is why every
REM shim in this directory is byte-identical. It also means the compiler
REM arguments in %* are forwarded completely untouched - no reconstructing an
REM argument list in batch, so quoting and -D flags survive intact.
REM
REM See ..\xc-dsc-find.bat for why the path is resolved at build time instead of
REM being written into platform.local.txt by an installer.
call "%~dp0..\xc-dsc-find.bat" xcdsc
if errorlevel 1 exit /b 1
"%XCDSC_DIR%\%~n0.exe" %*
exit /b %errorlevel%
