@echo off
setlocal
cd /d "%~dp0"

set "APP=%~dp0PhotoCircleAnimator.exe"
set "BUILD_APP=%~dp0PhotoCircleAnimator-new.exe"
set "LOG=%~dp0PhotoCircleAnimator-build.log"

set "COMPILER="
if exist "%SystemDrive%\msys64\ucrt64\bin\g++.exe" set "COMPILER=%SystemDrive%\msys64\ucrt64\bin\g++.exe"
if not defined COMPILER for /f "delims=" %%G in ('where g++ 2^>nul') do if not defined COMPILER set "COMPILER=%%G"

if not defined COMPILER (
    >"%LOG%" echo MinGW-w64 g++ was not found.
    >>"%LOG%" echo Install MSYS2 with the UCRT64 MinGW-w64 toolchain, then try again.
    exit /b 1
)

if exist "%BUILD_APP%" del /q "%BUILD_APP%"

>"%LOG%" echo Rebuilding the latest Photo Circle Animator source...
"%COMPILER%" -std=c++17 -O2 -mwindows -static-libgcc -static-libstdc++ "%~dp0main.cpp" -lgdiplus -lcomdlg32 -lgdi32 -luser32 -o "%BUILD_APP%" >>"%LOG%" 2>&1
if errorlevel 1 exit /b 1
if not exist "%BUILD_APP%" (
    >>"%LOG%" echo The compiler finished without creating the updated application.
    exit /b 1
)

move /Y "%BUILD_APP%" "%APP%" >>"%LOG%" 2>&1
if errorlevel 1 (
    >>"%LOG%" echo Could not replace the old application. Close PhotoCircleAnimator.exe and run the launcher again.
    exit /b 1
)

:run_app
start "" "%APP%"
exit /b 0
