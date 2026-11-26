[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuiltExe,
    [Parameter(Mandatory = $true)][string]$QtBinDir,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [string]$VcpkgBinDir = ""
)

$ErrorActionPreference = "Stop"

$SourceDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$DistDir = Join-Path $SourceDir "distribution"
$Version = (Get-Content (Join-Path $SourceDir "VERSION.txt") -Raw).Trim()
$Edition = "AuraPet-$Version-Windows-x64-Light"

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputAbs = (Resolve-Path $OutputDir).Path
$WorkDir = Join-Path $OutputAbs ".assembly-$([Guid]::NewGuid().ToString('N').Substring(0,8))"
$StageDir = Join-Path $WorkDir $Edition
$AppDir = Join-Path $StageDir "AuraPet"

try {
    New-Item -ItemType Directory -Path $AppDir, (Join-Path $StageDir "voice"), (Join-Path $StageDir "licenses") -Force | Out-Null

    Copy-Item -Path $BuiltExe -Destination (Join-Path $AppDir "AuraPet.exe") -Force

    # Deploy Qt6 runtime & plugins (Widgets, Sql, Network, GIF/PNG/JPEG)
    $windeployqt = Join-Path $QtBinDir "windeployqt.exe"
    if (-not (Test-Path $windeployqt)) {
        throw "windeployqt.exe not found in $QtBinDir"
    }
    & $windeployqt --release --no-translations --no-compiler-runtime --dir $AppDir (Join-Path $AppDir "AuraPet.exe")

    # Copy vcpkg runtime DLLs (libavif, aom, dav1d, archive, zlib, etc.)
    if ($VcpkgBinDir -and (Test-Path $VcpkgBinDir)) {
        Get-ChildItem -Path $VcpkgBinDir -Filter "*.dll" | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination $AppDir -Force
        }
    }

    # Copy launcher scripts, docs, voice service scripts & licenses
    Copy-Item -Path (Join-Path $SourceDir "VERSION.txt") -Destination (Join-Path $StageDir "VERSION.txt") -Force
    Copy-Item -Path (Join-Path $DistDir "安装 AuraPet.bat") -Destination $StageDir -Force
    Copy-Item -Path (Join-Path $DistDir "install-windows.ps1") -Destination $StageDir -Force
    Copy-Item -Path (Join-Path $DistDir "开始使用.md") -Destination $StageDir -Force

    foreach ($voiceFile in @("server.py", "metal_backend.py", "requirements.txt", "README.md", "voices.example.json")) {
        $src = Join-Path $SourceDir "voice\$voiceFile"
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination (Join-Path $StageDir "voice") -Force
        }
    }
    Copy-Item -Path (Join-Path $DistDir "models.sha256") -Destination (Join-Path $StageDir "voice") -Force
    Copy-Item -Path (Join-Path $DistDir "第三方组件.md") -Destination (Join-Path $StageDir "licenses") -Force
    Copy-Item -Path (Join-Path $SourceDir "LICENSE") -Destination (Join-Path $StageDir "licenses") -Force

    $ZipPath = Join-Path $OutputAbs "$Edition.zip"
    if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
    Compress-Archive -Path $StageDir -DestinationPath $ZipPath -CompressionLevel Optimal

    $hash = (Get-FileHash -Path $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -Path (Join-Path $OutputAbs "AuraPet-$Version-Windows-SHA256SUMS.txt") -Value "$hash  $Edition.zip" -Encoding UTF8
    Write-Host "Created $ZipPath"
} finally {
    if (Test-Path $WorkDir) {
        Remove-Item -Path $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
