@echo off
REM ===========================================================================
REM  ipecmd-upload.bat - program a dsPIC33CK through MPLAB X's ipecmd.
REM
REM      ipecmd-upload.bat <TP-code> <device> <hexfile>
REM      ipecmd-upload.bat <TP-code> <device> --erase
REM
REM  e.g. ipecmd-upload.bat nEDBG 33CK256MC005 C:\...\NanoBlink.ino.hex
REM
REM  <TP-code> is ipecmd's own -TP short name: PK4, PK5, SNAP, PKOB4, nEDBG
REM  (see the MPLAB X doc "Readme for IPECMD").
REM
REM  --erase exists because arduino-cli's burn-bootloader runs two recipes,
REM  erase.pattern then bootloader.pattern, and refuses to start if either key is
REM  missing. ipecmd's -M erases the whole device anyway, so the erase pass is
REM  strictly redundant - but it is wired to a real -E rather than a no-op, so
REM  that "Burn Bootloader" leaves a deterministic state even if programming then
REM  fails, and so nothing here quietly pretends to do something it does not do.
REM
REM  There is deliberately no check here that the board being burned actually has a
REM  bootloader to burn. arduino-cli already refuses first, and more clearly: on a
REM  board with no bootloader menu it stops at "Property 'bootloader.tool.serial' is
REM  undefined" without running either recipe, so nothing can reach the chip. A
REM  second guard in this wrapper only duplicated that, and it made Burn Bootloader
REM  conditional on a file path matching - a way to fail on a board that is in fact
REM  perfectly burnable. All five programmers erase and program unconditionally.
REM
REM  WHY A WRAPPER, for all five programmers
REM   1. ipecmd.exe lives inside MPLAB X, which cannot be shipped with this
REM      platform, and whose version is not predictable - this machine has four
REM      installed side by side. So its directory is discovered at upload time;
REM      see xc-dsc-find.bat. platform.txt holds no path for it at all.
REM   2. On Curiosity Nano boards, programming leaves the nEDBG's USB-CDC bridge
REM      wedged: the virtual COM port still enumerates and opens, but delivers
REM      zero bytes, so the Serial Monitor looks dead after every upload.
REM      Rebooting the debugger over USB clears it. That has to happen here
REM      because the IDE runs upload.pattern as a single argv, not through a
REM      shell, so two commands cannot be chained in platform.txt.
REM ===========================================================================

setlocal

set "TPCODE=%~1"
set "MCU=%~2"
set "HEXFILE=%~3"

if "%TPCODE%"=="" goto :usage
if "%MCU%"=="" goto :usage
if "%HEXFILE%"=="" goto :usage

call "%~dp0xc-dsc-find.bat" ipe
if errorlevel 1 exit /b 1

REM --- Step 1: erase, or program the device ----------------------------------
REM Each branch is a label rather than a parenthesised if/else block, because
REM %errorlevel% inside a block expands when the block is PARSED, not when the
REM command in it runs - so the exit code checked would be the one from before
REM ipecmd was even called.
if /i "%HEXFILE%"=="--erase" goto :erase

"%IPE_DIR%\ipecmd.exe" -TP%TPCODE% -P%MCU% -M -OL -F"%HEXFILE%"
set "RC=%errorlevel%"
if not "%RC%"=="0" (
    echo.
    echo Programming failed ^(ipecmd exit code %RC%^).
    exit /b %RC%
)
goto :reboot

:erase
"%IPE_DIR%\ipecmd.exe" -TP%TPCODE% -P%MCU% -E -OL
set "RC=%errorlevel%"
if not "%RC%"=="0" (
    echo.
    echo Erase failed ^(ipecmd exit code %RC%^).
    exit /b %RC%
)
REM An erase is never the last step of anything, so leave the CDC reboot to the
REM program pass that follows it.
exit /b 0

:reboot

REM --- Step 2: unwedge the nEDBG CDC bridge ---------------------------------
REM Only this one debugger needs it, and only after a successful program: doing
REM it after a failure would bury ipecmd's own error message under the reboot
REM output.
if /i not "%TPCODE%"=="nEDBG" exit /b 0

REM pymcuprog is an optional extra, and users of the PICkit4/PKOB4 boards have
REM no reason to have installed it. A missing optional tool must not fail an
REM upload that actually succeeded.
where pymcuprog >nul 2>&1
if not "%errorlevel%"=="0" (
    echo.
    echo Note: pymcuprog not found on PATH, so the nEDBG serial bridge was not
    echo       reset. If the Serial Monitor shows nothing, run
    echo       "pip install pymcuprog" once, or unplug/replug the board.
    exit /b 0
)

echo.
echo Rebooting nEDBG to restore the serial bridge...
pymcuprog reboot-debugger
if not "%errorlevel%"=="0" (
    echo Note: reboot-debugger failed. Unplug and replug the board if the
    echo       Serial Monitor stays silent.
    exit /b 0
)

REM Wait for USB re-enumeration so the IDE can reopen the port straight away.
REM "ping -n" rather than "timeout /t": timeout errors out with "input
REM redirection is not supported" when stdin is redirected, which it is here.
ping -n 8 127.0.0.1 >nul 2>&1

echo Serial bridge ready.
exit /b 0

REM ===========================================================================
:usage
echo ipecmd-upload.bat: usage: ipecmd-upload.bat ^<TP-code^> ^<device^> ^<hexfile^> 1>&2
echo   or: ipecmd-upload.bat ^<TP-code^> ^<device^> --erase 1>&2
echo   TP-code is one of PK4, PK5, SNAP, PKOB4, nEDBG 1>&2
exit /b 2
