@echo off
rem Wrapper for run.ps1: bypasses the PowerShell execution policy that blocks
rem unsigned .ps1 files. Comments are ASCII on purpose: cmd.exe reads .cmd
rem files in the OEM code page, so non-ASCII text here gets garbled.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" %*
exit /b %ERRORLEVEL%
