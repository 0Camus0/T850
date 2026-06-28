@echo off
setlocal

for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI\"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%BuildAndroidFastApk.ps1" %*
exit /b %ERRORLEVEL%
