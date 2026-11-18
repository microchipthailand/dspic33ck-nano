@echo off
rem Build NanoBootloader.exe (GUI), nanobl.exe (command line) and the test runner with the C# compiler that
rem ships with Windows (.NET Framework 4.x). No SDK or Visual Studio needed.
setlocal
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
if not exist "%CSC%" ( echo csc.exe not found - .NET Framework 4.x is required & exit /b 1 )
cd /d "%~dp0"
if not exist bin mkdir bin
set COMMON=src\Protocol.cs src\Image.cs src\Uploader.cs src\Cli.cs
set REFS=/r:System.dll /r:System.Management.dll
set OPT=/nologo /optimize+ /warn:4 /platform:anycpu
"%CSC%" %OPT% /target:exe /out:bin\nanobl.exe /main:NanoBootloader.Cli %REFS% %COMMON% || exit /b 1
"%CSC%" %OPT% /target:winexe /out:bin\NanoBootloader.exe /main:NanoBootloader.MainForm %REFS% /r:System.Windows.Forms.dll /r:System.Drawing.dll %COMMON% src\Gui.cs || exit /b 1
"%CSC%" %OPT% /target:exe /out:bin\NanoBootloaderTests.exe /main:NanoBootloader.Tests %REFS% %COMMON% test\Emulator.cs test\Tests.cs || exit /b 1
echo built bin\nanobl.exe bin\NanoBootloader.exe bin\NanoBootloaderTests.exe
