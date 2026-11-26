@echo off
chcp 65001 >nul
setlocal
set "SCRIPT_DIR=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%install-windows.ps1" %*
if errorlevel 1 (
    echo.
    echo [AuraPet] 安装未完成，请查看上方提示信息。
    pause
)
endlocal
