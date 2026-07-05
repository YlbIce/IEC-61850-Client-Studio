@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
cmake --build "D:\WORKSPACE\Electron\Iec61850ClientStudio\backend\build-tests" --config Release --target studio-tests
exit /b %errorlevel%
