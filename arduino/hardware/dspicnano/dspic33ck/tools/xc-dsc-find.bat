@echo off
REM ===========================================================================
REM  xc-dsc-find.bat - locate the XC-DSC compiler bin/ and the MPLAB X IPE dir.
REM
REM  WHY THIS EXISTS
REM  Arduino's Board Manager only downloads and unzips archives; it runs no
REM  post-install script, by design. So there is nowhere for a URL-based install
REM  to auto-detect these paths the way install_arduino_ide.bat does at install
REM  time. The Device Family Packs sidestep the problem entirely (they are
REM  Apache-2.0, so they ship as Board Manager tool packs and their paths are
REM  deterministic) but the XC-DSC compiler licence is explicitly
REM  "non-transferable" and MPLAB X IPE likewise cannot be redistributed. Both
REM  stay user-installed prerequisites, and get located HERE - lazily, the first
REM  time a build or an upload actually needs them.
REM
REM  CALLED AS A SUBROUTINE. There is deliberately no setlocal: the whole point
REM  is to export the result to the caller.
REM
REM      call "%%~dp0..\xc-dsc-find.bat" xcdsc     -> sets XCDSC_DIR
REM      call "%%~dp0..\xc-dsc-find.bat" ipe       -> sets IPE_DIR
REM      if errorlevel 1 exit /b 1
REM
REM  RESOLUTION ORDER (first hit wins)
REM    1. platform.local.txt   - build.compiler.path / build.tools.mplab.path.
REM                              Read straight from the file rather than passed
REM                              in as an argument, so platform.txt recipes stay
REM                              free of machine-specific plumbing and a
REM                              hand-written override keeps working unchanged.
REM    2. %XCDSC_PATH% / %MPLABX_IPE_PATH%   - environment override.
REM    3. cache under %LOCALAPPDATA% - used only if the tool is still there.
REM    4. glob the default install roots, newest version first.
REM    5. fail loudly with an actionable message. A missing compiler must never
REM       degrade to a confusing "command not found" halfway through a build.
REM ===========================================================================

if /i "%~1"=="xcdsc" goto :want_xcdsc
if /i "%~1"=="ipe"   goto :want_ipe
echo xc-dsc-find.bat: unknown target "%~1" (expected "xcdsc" or "ipe") 1>&2
exit /b 2

REM ---------------------------------------------------------------------------
:want_xcdsc
REM Already resolved earlier in this same shell? (only helps within one script)
if defined XCDSC_DIR if exist "%XCDSC_DIR%\xc-dsc-g++.exe" exit /b 0
set "_XF_SENT=xc-dsc-g++.exe"
set "_XF_KEY=build.compiler.path"
set "_XF_ENV=%XCDSC_PATH%"
set "_XF_CACHE=%LOCALAPPDATA%\Microchip\Arduino_dsPIC33CK\xcdsc.path"
set "_XF_GLOB=Microchip\xc-dsc"
set "_XF_SUB=bin"
call :resolve
if not defined _XF_RESULT goto :fail_xcdsc
set "XCDSC_DIR=%_XF_RESULT%"
call :cleanup
exit /b 0

REM ---------------------------------------------------------------------------
:want_ipe
if defined IPE_DIR if exist "%IPE_DIR%\ipecmd.exe" exit /b 0
set "_XF_SENT=ipecmd.exe"
set "_XF_KEY=build.tools.mplab.path"
set "_XF_ENV=%MPLABX_IPE_PATH%"
set "_XF_CACHE=%LOCALAPPDATA%\Microchip\Arduino_dsPIC33CK\ipe.path"
set "_XF_GLOB=Microchip\MPLABX"
set "_XF_SUB=mplab_platform\mplab_ipe"
call :resolve
if not defined _XF_RESULT goto :fail_ipe
set "IPE_DIR=%_XF_RESULT%"
call :cleanup
exit /b 0

REM ===========================================================================
:resolve
set "_XF_RESULT="
set "_XF_FROMGLOB="
REM tools\ -> platform root
set "_XF_ROOT=%~dp0.."

REM --- 1. platform.local.txt -------------------------------------------------
if exist "%_XF_ROOT%\platform.local.txt" (
  for /f "usebackq eol=# tokens=1,* delims==" %%A in ("%_XF_ROOT%\platform.local.txt") do (
    if /i "%%A"=="%_XF_KEY%" call :try "%%B"
  )
)
if defined _XF_RESULT goto :eof

REM --- 2. environment override ----------------------------------------------
if defined _XF_ENV call :try "%_XF_ENV%"
if defined _XF_RESULT goto :eof

REM --- 3. cache -------------------------------------------------------------
if exist "%_XF_CACHE%" (
  for /f "usebackq delims=" %%A in ("%_XF_CACHE%") do call :try "%%A"
)
if defined _XF_RESULT goto :eof

REM --- 4. glob the default install roots, newest first ----------------------
REM /o-n is a reverse *name* sort, which orders every version that exists today
REM correctly (v6.35 > v6.30 > ... , v4.10 > v4.00). It would mis-order a future
REM double-digit major (v10.00 would sort below v4.00); revisit if that happens.
set "_XF_PF=%ProgramFiles%"
set "_XF_PF86=%ProgramFiles(x86)%"
call :glob "%_XF_PF%"
if defined _XF_RESULT goto :cache_it
if not "%_XF_PF86%"=="" call :glob "%_XF_PF86%"
if defined _XF_RESULT goto :cache_it
goto :eof

:cache_it
REM Only glob hits are cached. An override must never get frozen into the cache,
REM or removing it later would silently have no effect.
if not defined _XF_FROMGLOB goto :eof
if not exist "%LOCALAPPDATA%\Microchip\Arduino_dsPIC33CK" (
  mkdir "%LOCALAPPDATA%\Microchip\Arduino_dsPIC33CK" 2>nul
)
REM Arduino compiles in parallel, so several of these race. Write a private temp
REM file and move it into place, so a reader never sees a half-written cache.
REM %RANDOM% is captured once: it re-rolls on every expansion.
set "_XF_TMP=%_XF_CACHE%.%RANDOM%%RANDOM%.tmp"
> "%_XF_TMP%" echo %_XF_RESULT%
move /y "%_XF_TMP%" "%_XF_CACHE%" >nul 2>nul
if exist "%_XF_TMP%" del "%_XF_TMP%" 2>nul
set "_XF_TMP="
goto :eof

REM --- glob one install root -------------------------------------------------
:glob
if not exist "%~1\%_XF_GLOB%" goto :eof
for /f "delims=" %%D in ('dir /b /ad /o-n "%~1\%_XF_GLOB%\v*" 2^>nul') do (
  if not defined _XF_RESULT call :try_glob "%~1\%_XF_GLOB%\%%D\%_XF_SUB%"
)
goto :eof

:try_glob
if defined _XF_RESULT goto :eof
call :try "%~1"
if defined _XF_RESULT set "_XF_FROMGLOB=1"
goto :eof

REM --- test one candidate directory -----------------------------------------
REM Normalises forward slashes (platform.local.txt uses them) and strips any
REM trailing separator, so callers can always join with a single backslash.
:try
if defined _XF_RESULT goto :eof
set "_XF_C=%~1"
if not defined _XF_C goto :eof
set "_XF_C=%_XF_C:/=\%"
if "%_XF_C:~-1%"=="\" set "_XF_C=%_XF_C:~0,-1%"
if not exist "%_XF_C%\%_XF_SENT%" goto :eof
set "_XF_RESULT=%_XF_C%"
goto :eof

REM ===========================================================================
:fail_xcdsc
echo. 1>&2
echo *** XC-DSC compiler not found. 1>&2
echo *** 1>&2
echo *** Arduino_dsPIC33CK needs the MPLAB XC-DSC compiler v4.00 or later. 1>&2
echo *** It cannot be bundled with this platform: its licence is 1>&2
echo *** non-transferable, so it has to be installed separately (it is free). 1>&2
echo *** 1>&2
echo ***   Download: https://www.microchip.com/mplab/compilers 1>&2
echo *** 1>&2
echo *** Looked for xc-dsc-g++.exe in: 1>&2
echo ***   %%ProgramFiles%%\Microchip\xc-dsc\v*\bin 1>&2
echo ***   %%XCDSC_PATH%% 1>&2
echo *** 1>&2
echo *** Installed somewhere else? Create platform.local.txt next to 1>&2
echo *** platform.txt containing: 1>&2
echo ***   build.compiler.path=D:/your/path/to/xc-dsc/v4.00/bin/ 1>&2
echo *** then restart the Arduino IDE. 1>&2
echo. 1>&2
call :cleanup
exit /b 1

:fail_ipe
echo. 1>&2
echo *** MPLAB X IPE not found. 1>&2
echo *** 1>&2
echo *** Uploading with PICkit 4 / PICkit 5 / SNAP / PKOB4 / nEDBG needs 1>&2
echo *** ipecmd.exe, which ships with MPLAB X IDE. It cannot be bundled with 1>&2
echo *** this platform, so it has to be installed separately (it is free). 1>&2
echo *** 1>&2
echo ***   Download: https://www.microchip.com/mplab/mplab-x-ide 1>&2
echo *** 1>&2
echo *** Looked for ipecmd.exe in: 1>&2
echo ***   %%ProgramFiles%%\Microchip\MPLABX\v*\mplab_platform\mplab_ipe 1>&2
echo ***   %%MPLABX_IPE_PATH%% 1>&2
echo *** 1>&2
echo *** Installed somewhere else? Create platform.local.txt next to 1>&2
echo *** platform.txt containing: 1>&2
echo ***   build.tools.mplab.path=D:/your/path/MPLABX/v6.35/mplab_platform/mplab_ipe 1>&2
echo *** then restart the Arduino IDE. 1>&2
echo. 1>&2
call :cleanup
exit /b 1

REM ===========================================================================
:cleanup
set "_XF_SENT="
set "_XF_KEY="
set "_XF_ENV="
set "_XF_CACHE="
set "_XF_GLOB="
set "_XF_SUB="
set "_XF_ROOT="
set "_XF_RESULT="
set "_XF_FROMGLOB="
set "_XF_C="
set "_XF_PF="
set "_XF_PF86="
set "_XF_TMP="
goto :eof
