@echo off
setlocal
cd /d "%~dp0.."
if not exist dist mkdir dist
msbuild WinInput.sln /p:Configuration=Release /p:Platform="Any CPU" /m
if errorlevel 1 exit /b 1
xcopy /E /I /Y src\WinInput.Receiver\bin\Release dist\Receiver
xcopy /E /I /Y src\WinInput.Sender\bin\Release dist\Sender
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path dist\Receiver,dist\Sender,README.md -DestinationPath dist\WinInput.zip -Force"
echo dist\WinInput.zip
