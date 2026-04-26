@echo off
setlocal

set "DEST_DIR=%~1"
if "%DEST_DIR%"=="" exit /b 1
shift /1

set "CANDIDATES="

if not exist "%DEST_DIR%" mkdir "%DEST_DIR%"

:next
if "%~1"=="" goto not_found
set "CANDIDATES=%CANDIDATES% %~1"
if exist "%~1" (
  copy /Y "%~1" "%DEST_DIR%"
  exit /b %ERRORLEVEL%
)
shift /1
goto next

:not_found
echo None of the source files exist:%CANDIDATES% 1>&2
exit /b 1
