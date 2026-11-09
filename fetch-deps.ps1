# Fetches the Qt 6 that OBS itself builds against, so the dock can be compiled
# with headers and import libraries that exactly match the Qt DLLs OBS ships.
#   .\fetch-deps.ps1            download + extract into deps\qt
#   .\fetch-deps.ps1 -Force     re-download even if deps\qt already exists
#
# This is the OBS project's own CI dependency bundle (obsproject/obs-deps), not
# a Qt installer, which keeps the version pinned to whatever OBS 33 was built
# with. Roughly 250 MB, extracted next to the source (deps\qt is gitignored).

param(
    [switch]$Force,
    [string]$Tag = '2026-10-23'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$dest = Join-Path $root 'deps\qt'

if ((Test-Path (Join-Path $dest 'include\QtWidgets\QWidget')) -and -not $Force) {
    Write-Host "Qt already present in deps\qt (use -Force to refresh)."
    exit 0
}

$file = "windows-deps-qt6-$Tag-x64.zip"
$url = "https://github.com/obsproject/obs-deps/releases/download/$Tag/$file"
$tmp = Join-Path $env:TEMP $file

if (-not (Test-Path $tmp) -or $Force) {
    Write-Host "Downloading $file (about 250 MB) ..."
    $progress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    try {
        Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing -TimeoutSec 900
    }
    finally {
        $ProgressPreference = $progress
    }
} else {
    Write-Host "Using cached $tmp"
}

if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Write-Host "Extracting ..."
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory($tmp, $dest)

if (-not (Test-Path (Join-Path $dest 'include\QtWidgets\QWidget'))) {
    throw "Extraction did not produce include\QtWidgets - unexpected archive layout"
}

Write-Host ""
Write-Host "Qt ready at deps\qt"
Write-Host "build.ps1 picks it up automatically; the dock will be built."
