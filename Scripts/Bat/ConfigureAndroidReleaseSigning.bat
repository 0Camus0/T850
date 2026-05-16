@echo off
set "ROOT=%~dp0..\..\"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%ConfigureAndroidReleaseSigning.ps1" %*