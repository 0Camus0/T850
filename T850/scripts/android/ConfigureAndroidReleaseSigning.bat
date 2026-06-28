@echo off
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI\"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%ConfigureAndroidReleaseSigning.ps1" %*