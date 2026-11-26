[CmdletBinding()]
param(
    [ValidateSet("Choose", "AppOnly", "WithVoice", "VoiceOnly")]
    [string]$Mode = "Choose",
    [switch]$NoLaunch,
    [switch]$UseMirror
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$BundleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$VersionFile = Join-Path $BundleDir "VERSION.txt"
$Version = if (Test-Path $VersionFile) { (Get-Content $VersionFile -Raw).Trim() } else { "1.0.34" }

$InstallDir = Join-Path $env:LOCALAPPDATA "AuraPet"
$AppDir = Join-Path $InstallDir "app"
$DataDir = Join-Path $env:LOCALAPPDATA "Gif-Pet\Gif-Pet"
$VoiceDataDir = Join-Path $DataDir "voice"
$SourceAppDir = Join-Path $BundleDir "AuraPet"

if ($Mode -eq "Choose") {
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host "  AuraPet $Version Windows 懒人一键部署" -ForegroundColor Cyan
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "请选择安装模式："
    Write-Host "  [1] 仅安装 AuraPet 桌面应用（推荐，开箱即用，支持文字聊天与系统语音）"
    Write-Host "  [2] 安装 AuraPet + 配置本机千问语音服务目录（需本机 Python 3.11/3.12）"
    Write-Host "  [3] 仅配置本机千问语音服务目录"
    Write-Host "  [0] 取消安装"
    Write-Host ""
    $choice = Read-Host "请输入编号 (默认 1)"
    switch ($choice.Trim()) {
        ""  { $Mode = "AppOnly" }
        "1" { $Mode = "AppOnly" }
        "2" { $Mode = "WithVoice" }
        "3" { $Mode = "VoiceOnly" }
        "0" { Write-Host "已取消安装。"; exit 0 }
        default { Write-Host "无效选项，已退出。"; exit 2 }
    }
}

function Stop-RunningAuraPet {
    $procs = Get-Process -Name "AuraPet" -ErrorAction SilentlyContinue
    if ($procs) {
        Write-Host "正在关闭运行中的 AuraPet..." -ForegroundColor Yellow
        $procs | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 600
    }
}

function New-Shortcut {
    param([string]$ShortcutPath, [string]$TargetPath, [string]$WorkingDir, [string]$Description)
    $wsh = New-Object -ComObject WScript.Shell
    $sc = $wsh.CreateShortcut($ShortcutPath)
    $sc.TargetPath = $TargetPath
    $sc.WorkingDirectory = $WorkingDir
    $sc.Description = $Description
    $sc.Save()
}

function Register-GifPetAssociation {
    param([string]$ExePath)
    $classesRoot = "HKCU:\Software\Classes"
    New-Item -Path "$classesRoot\.gifpet" -Force | Out-Null
    Set-ItemProperty -Path "$classesRoot\.gifpet" -Name "(Default)" -Value "AuraPet.RolePackage"
    New-Item -Path "$classesRoot\AuraPet.RolePackage" -Force | Out-Null
    Set-ItemProperty -Path "$classesRoot\AuraPet.RolePackage" -Name "(Default)" -Value "AuraPet 角色包"
    New-Item -Path "$classesRoot\AuraPet.RolePackage\DefaultIcon" -Force | Out-Null
    Set-ItemProperty -Path "$classesRoot\AuraPet.RolePackage\DefaultIcon" -Name "(Default)" -Value "`"$ExePath`",0"
    New-Item -Path "$classesRoot\AuraPet.RolePackage\shell\open\command" -Force | Out-Null
    Set-ItemProperty -Path "$classesRoot\AuraPet.RolePackage\shell\open\command" -Name "(Default)" -Value "`"$ExePath`" `"%1`""
}

function Install-DesktopApp {
    if (-not (Test-Path (Join-Path $SourceAppDir "AuraPet.exe"))) {
        throw "未找到 $SourceAppDir\AuraPet.exe，请先完整解压发行包后再运行安装脚本。"
    }
    Stop-RunningAuraPet

    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    $stageDir = Join-Path $InstallDir ".stage-$([Guid]::NewGuid().ToString('N').Substring(0,8))"
    Copy-Item -Path $SourceAppDir -Destination $stageDir -Recurse -Force

    if (Test-Path $AppDir) {
        $backupDir = Join-Path $InstallDir ".backup-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
        Move-Item -Path $AppDir -Destination $backupDir -Force
        try {
            Move-Item -Path $stageDir -Destination $AppDir -Force
            Remove-Item -Path $backupDir -Recurse -Force -ErrorAction SilentlyContinue
        } catch {
            Move-Item -Path $backupDir -Destination $AppDir -Force -ErrorAction SilentlyContinue
            throw
        }
    } else {
        Move-Item -Path $stageDir -Destination $AppDir -Force
    }

    $exePath = Join-Path $AppDir "AuraPet.exe"
    $desktopShortcut = Join-Path ([Environment]::GetFolderPath("Desktop")) "AuraPet.lnk"
    $startMenuDir = Join-Path ([Environment]::GetFolderPath("Programs")) "AuraPet"
    New-Item -ItemType Directory -Path $startMenuDir -Force | Out-Null
    $startShortcut = Join-Path $startMenuDir "AuraPet.lnk"

    New-Shortcut -ShortcutPath $desktopShortcut -TargetPath $exePath -WorkingDir $AppDir -Description "AuraPet 桌面伙伴"
    New-Shortcut -ShortcutPath $startShortcut -TargetPath $exePath -WorkingDir $AppDir -Description "AuraPet 桌面伙伴"
    Register-GifPetAssociation -ExePath $exePath

    Write-Host "[成功] AuraPet $Version 已安装到：$AppDir" -ForegroundColor Green
    Write-Host "[成功] 已创建桌面与开始菜单快捷方式，并关联 .gifpet 角色包。" -ForegroundColor Green
}

function Install-VoiceService {
    $sourceVoice = Join-Path $BundleDir "voice"
    if (-not (Test-Path (Join-Path $sourceVoice "server.py"))) {
        throw "当前安装包未包含 voice\server.py。"
    }
    New-Item -ItemType Directory -Path $VoiceDataDir -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $VoiceDataDir "logs") -Force | Out-Null
    Copy-Item -Path (Join-Path $sourceVoice "*") -Destination $VoiceDataDir -Recurse -Force

    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) { $python = Get-Command py -ErrorAction SilentlyContinue }
    if (-not $python) {
        Write-Host "[提示] 未检测到 Python，请先安装 Python 3.11 或 3.12（勾选 Add Python to PATH），然后运行 $VoiceDataDir\start-voice.cmd。" -ForegroundColor Yellow
    } else {
        $venvDir = Join-Path $VoiceDataDir "venv"
        if (-not (Test-Path (Join-Path $venvDir "Scripts\python.exe"))) {
            Write-Host "正在创建本地千问语音虚拟环境..." -ForegroundColor Cyan
            & $python.Source -m venv $venvDir
        }
        $venvPy = Join-Path $venvDir "Scripts\python.exe"
        if (Test-Path (Join-Path $VoiceDataDir "requirements.txt")) {
            Write-Host "正在安装语音服务 Python 依赖..." -ForegroundColor Cyan
            & $venvPy -m pip install --upgrade pip | Out-Null
            & $venvPy -m pip install -r (Join-Path $VoiceDataDir "requirements.txt")
        }
    }

    $startCmd = @"
@echo off
chcp 65001 >nul
setlocal
set "VOICE_DIR=%~dp0"
if exist "%VOICE_DIR%venv\Scripts\python.exe" (
    set "PY=%VOICE_DIR%venv\Scripts\python.exe"
) else (
    set "PY=python"
)
start "AuraPet-Qwen-TTS" /min "%PY%" "%VOICE_DIR%server.py" --backend torch --voices "%VOICE_DIR%voices.json"
endlocal
"@
    $stopCmd = @"
@echo off
powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { `$_.CommandLine -like '*server.py*' -and `$_.CommandLine -like '*AuraPet*' -or `$_.CommandLine -like '*Gif-Pet*voice*server.py*' } | ForEach-Object { Stop-Process -Id `$_.ProcessId -Force -ErrorAction SilentlyContinue }"
"@
    Set-Content -Path (Join-Path $VoiceDataDir "start-voice.cmd") -Value $startCmd -Encoding UTF8
    Set-Content -Path (Join-Path $VoiceDataDir "stop-voice.cmd") -Value $stopCmd -Encoding UTF8
    Write-Host "[成功] 语音服务目录已部署至：$VoiceDataDir" -ForegroundColor Green
    Write-Host "[提示] 请参阅 $VoiceDataDir\README.md 放置或下载 Qwen3-TTS 权重后，可在桌宠菜单「本机千问语音服务…」一键启停。" -ForegroundColor Cyan
}

if ($Mode -ne "VoiceOnly") { Install-DesktopApp }
if ($Mode -ne "AppOnly") { Install-VoiceService }

if ($Mode -ne "VoiceOnly" -and -not $NoLaunch) {
    Start-Process -FilePath (Join-Path $AppDir "AuraPet.exe") -WorkingDirectory $AppDir
}
