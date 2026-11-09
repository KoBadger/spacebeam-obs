# Build a distributable zip of the plugin.
#   .\package.ps1
#
# Produces dist\spacebeam-<version>.zip laid out the way OBS expects a plugin
# archive to look, so users can extract it straight into
# C:\ProgramData\obs-studio\plugins\

param(
    [switch]$NoBuild,
    [string]$OutDir = "dist"
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
if (-not $version) { throw "VERSION file is empty" }

if (-not $NoBuild) {
    Write-Host "Building ..."
    & (Join-Path $root 'build.ps1')
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

$dll = Join-Path $root 'build\spacebeam.dll'
if (-not (Test-Path $dll)) { throw "Build first: .\build.ps1" }

$stage = Join-Path $root "$OutDir\stage\spacebeam"
if (Test-Path (Join-Path $root "$OutDir\stage")) {
    Remove-Item (Join-Path $root "$OutDir\stage") -Recurse -Force
}
New-Item -ItemType Directory -Force -Path "$stage\data\locale", "$stage\data\shaders" | Out-Null

Copy-Item $dll "$stage\spacebeam.dll" -Force

# The dock is a separate DLL so the filter itself never imports Qt.
$dockDll = Join-Path $root 'build\spacebeam_dock.dll'
$hasDock = Test-Path $dockDll
if ($hasDock) { Copy-Item $dockDll "$stage\spacebeam_dock.dll" -Force }
Copy-Item (Join-Path $root 'data\shaders\spacebeam.effect') "$stage\data\shaders\spacebeam.effect" -Force
Copy-Item (Join-Path $root 'data\locale\en-US.ini') "$stage\data\locale\en-US.ini" -Force



$manifest = @"
{
    "display_name": "SpaceBeam",
    "id": "3f0b1e52-9c74-4a6b-8f21-7d2c5e90b4a1",
    "version": "$version",
    "authors": ["Calmy Jane (original app)", "OBS port"],
    "name": "spacebeam",
    "description": "Real-time kaleidoscope / Droste / tunnel / swirl visual synthesizer ported from SpaceBeam.",
    "long_description": "Ported from the SpaceBeam Android app. Camera Transform, Color, Edge Detect, Kaleidoscope, Droste spiral, Master Transform, 3D Tunnel, Swirl, frame feedback, LFOs with tempo sync, audio reactivity, a 4-layer source mixer, preset slots with morphing and a control dock.",
    "urls": {"repository": "https://github.com/CalmyJane/SpaceBeam"},
    "has_banner": false,
    "has_icon": false
}
"@
Set-Content -Path "$stage\data\manifest.json" -Value $manifest -Encoding UTF8

$zip = Join-Path $root "$OutDir\spacebeam-$version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
# Archive the folder itself (not its contents) so the zip contains a top-level
# "spacebeam\" directory. OBS only finds a plugin inside plugins\<name>\.
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

# Publish a checksum next to the archive so users can verify the download.
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$sumFile = "$zip.sha256"
"$hash  $(Split-Path $zip -Leaf)" | Set-Content -Path $sumFile -Encoding ASCII

$sizeKb = [math]::Round((Get-Item $zip).Length / 1KB, 1)
Write-Host ""
Write-Host "Packaged: $zip ($sizeKb KB)"
if ($hasDock) {
    Write-Host "Includes the Qt dock panel."
} else {
    Write-Host "No dock panel in this build (Qt was not available at build time)."
}
Write-Host ""
Write-Host "Users extract the zip into:"
Write-Host "  C:\ProgramData\obs-studio\plugins\"
Write-Host "Checksum: $(Split-Path $sumFile -Leaf)  ($hash)"
