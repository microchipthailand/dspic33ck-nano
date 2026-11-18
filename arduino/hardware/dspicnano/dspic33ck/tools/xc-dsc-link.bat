@echo off
REM Wrapper for the xc-dsc-gcc link stage.
REM The GLD preprocessor (elf-cc1.exe) writes temp files to CWD, and Arduino
REM IDE's CWD may not be writable, so cd to TEMP first. This is load-bearing.
cd /d "%TEMP%"
REM CALL, not a bare %*: see suppress-stderr.bat. The shim resolves its own
REM location from %~dp0, so the cd above does not affect it.
call %* 2>nul
exit /b %errorlevel%
