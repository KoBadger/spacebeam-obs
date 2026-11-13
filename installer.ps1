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

# Optional code signing. There is no certificate in this project, so by default
# the installer is unsigned and SmartScreen warns about it (the README explains
# that and how to verify the checksum instead). If a certificate is supplied,
# signing happens here, before the hash is taken, so the .sha256 matches the
# signed file:
#   $env:SB_SIGN_CERT      = 'C:\path\to\cert.pfx'
#   $env:SB_SIGN_CERT_B64  = base64 of a .pfx (used by CI, from a secret)
#   $env:SB_SIGN_PASSWORD  = its password
$signCert = $env:SB_SIGN_CERT
$tempCert = $null
if (-not $signCert -and $env:SB_SIGN_CERT_B64) {
    $tempCert = Join-Path ([System.IO.Path]::GetTempPath()) "sb-sign-$([guid]::NewGuid()).pfx"
    [System.IO.File]::WriteAllBytes($tempCert, [Convert]::FromBase64String($env:SB_SIGN_CERT_B64))
    $signCert = $tempCert
}
try {
    if ($signCert) {
        $signtool = Get-ChildItem `
            (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\*\x64\signtool.exe'), `
            (Join-Path $env:ProgramFiles 'Windows Kits\10\bin\*\x64\signtool.exe') `
            -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
        if (-not $signtool) {
            throw "a signing certificate was supplied but signtool.exe was not found (install the Windows SDK)"
        }
        Write-Host "Signing installer ..."
        & $signtool.FullName sign /fd SHA256 /f $signCert /p $env:SB_SIGN_PASSWORD `
            /tr http://timestamp.digicert.com /td SHA256 $exe
        if ($LASTEXITCODE -ne 0) { throw "signtool failed" }
    } else {
        Write-Host "No signing certificate set - the installer will be unsigned (SmartScreen will warn)."
    }
}
finally {
    if ($tempCert -and (Test-Path $tempCert)) { Remove-Item $tempCert -Force }
}

$sizeMb = [math]::Round((Get-Item $exe).Length / 1MB, 2)
$hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path $exe -Leaf)" | Set-Content -Path "$exe.sha256" -Encoding ASCII

Write-Host ""
Write-Host "Installer: $exe ($sizeMb MB)"
Write-Host "Checksum : $(Split-Path $exe -Leaf).sha256"
if ($hasDock -eq '0') {
    Write-Host "No dock DLL in this build."
}
