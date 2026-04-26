@echo off
setlocal

set "LINK_PATH=%~1"
set "TARGET_PATH=%~2"

if "%LINK_PATH%"=="" exit /b 1
if "%TARGET_PATH%"=="" exit /b 1

if not exist "%~dp1" mkdir "%~dp1"

if exist "%LINK_PATH%" exit /b 0

mklink /J "%LINK_PATH%" "%TARGET_PATH%"
if %ERRORLEVEL%==0 exit /b 0

rem Another project may have created the same junction in parallel.
if exist "%LINK_PATH%" exit /b 0

exit /b %ERRORLEVEL%
