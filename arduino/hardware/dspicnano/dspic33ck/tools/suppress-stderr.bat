@echo off
REM Run a command with its stderr discarded, preserving the exit code.
REM
REM CALL, not a bare %*: the commands passed in are now the .bat shims in
REM tools/bin/ rather than .exe files, and invoking a batch file without CALL
REM would transfer control away from this script for good.
call %* 2>nul
exit /b %errorlevel%
