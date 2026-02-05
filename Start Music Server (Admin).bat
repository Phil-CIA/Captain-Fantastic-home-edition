@echo off
:: Captain Fantastic Music Server Launcher
:: This batch file will request admin privileges and start the music server

:: Check for admin rights
>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"

:: If not admin, re-launch with admin rights
if '%errorlevel%' NEQ '0' (
    echo Requesting administrative privileges...
    goto UACPrompt
) else (
    goto gotAdmin
)

:UACPrompt
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin.vbs"
    echo UAC.ShellExecute "%~s0", "", "", "runas", 1 >> "%temp%\getadmin.vbs"
    "%temp%\getadmin.vbs"
    exit /B

:gotAdmin
    if exist "%temp%\getadmin.vbs" ( del "%temp%\getadmin.vbs" )
    pushd "%~dp0"

:: Start the PowerShell music server
echo.
echo ========================================
echo  Captain Fantastic Music Server
echo ========================================
echo.
echo Starting server on port 8000...
echo.

powershell.exe -ExecutionPolicy Bypass -NoExit -File "%~dp0Start-MusicServer.ps1"
