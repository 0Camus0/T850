@echo off
setlocal

set "ROOT=%~dp0..\..\"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%BuildAndroidFastApk.ps1" %*
exit /b %ERRORLEVEL%
