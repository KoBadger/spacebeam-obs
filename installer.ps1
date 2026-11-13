# Build the Windows installer (SpaceBeam-<version>-windows-x64-setup.exe) from the staged
# plugin files. Staging is what package.ps1 produces, so this runs it first.
#
#   .\installer.ps1                 # build, then compile the installer
#   .\installer.ps1 -NoBuild        # reuse dist\stage as-is
#
# Requires Inno Setup 6 (winget install --id JRSoftware.InnoSetup).

param(
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
if (-not $version) { throw "VERSION file is empty" }

$iscc = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) {
    throw "ISCC.exe not found. Install it with: winget install --id JRSoftware.InnoSetup"
}

if (-not $NoBuild) {
    Write-Host "Staging plugin files ..."
    & (Join-Path $root 'package.ps1')
    if ($LASTEXITCODE -ne 0) { throw "package.ps1 failed" }
}

$stage = Join-Path $root 'dist\stage\spacebeam'
if (-not (Test-Path (Join-Path $stage 'bin\64bit\spacebeam.dll'))) {
    throw "Staging is missing: $stage\bin\64bit\spacebeam.dll (run .\package.ps1)"
}

$hasDock = if (Test-Path (Join-Path $stage 'bin\64bit\spacebeam_dock.dll')) { '1' } else { '0' }

Write-Host "Compiling installer with Inno Setup ..."
& $iscc `
    "/DVersion=$version" `
    "/DStageDir=$stage" `
    "/DHasDock=$hasDock" `
    (Join-Path $root 'installer\spacebeam.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }

$exe = Join-Path $root "dist\SpaceBeam-$version-windows-x64-setup.exe"
if (-not (Test-Path $exe)) { throw "installer was not produced at $exe" }

$sizeMb = [math]::Round((Get-Item $exe).Length / 1MB, 2)
$hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path $exe -Leaf)" | Set-Content -Path "$exe.sha256" -Encoding ASCII

Write-Host ""
Write-Host "Installer: $exe ($sizeMb MB)"
Write-Host "Checksum : $(Split-Path $exe -Leaf).sha256"
if ($hasDock -eq '0') {
    Write-Host "No dock DLL in this build."
}
