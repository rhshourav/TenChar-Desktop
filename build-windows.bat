@echo off
rem Double-click to build TenChar Desktop on Windows. The first run installs what is missing (run as administrator).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows\build.ps1" -InstallPrereqs %*
echo.
pause
