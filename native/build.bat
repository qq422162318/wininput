@echo off
setlocal
cd /d "%~dp0"
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
if /i "%~1"=="sender" goto sender
cl /nologo /EHsc /std:c++14 /utf-8 /O2 /MT /DUNICODE /D_UNICODE receiver.cpp /link /SUBSYSTEM:WINDOWS user32.lib /OUT:bin\WinInput.Receiver.exe
if errorlevel 1 exit /b 1
:sender
cl /nologo /EHsc /std:c++14 /utf-8 /O2 /MT /DUNICODE /D_UNICODE sender.cpp /link /SUBSYSTEM:WINDOWS user32.lib /OUT:bin\WinInput.Sender.exe
if errorlevel 1 exit /b 1
echo Build complete: %CD%\bin
