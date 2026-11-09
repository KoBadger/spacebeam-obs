# Install the built plugin into OBS Studio's shared plugin folder.
#   .\install.ps1                 install to ProgramData
#   .\install.ps1 -Destination X  install somewhere else
#   .\install.ps1 -Uninstall      remove it

param(
    [string]$Destination = "C:\ProgramData\obs-studio\plugins\spacebeam",
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()

if ($Uninstall) {
    if (Test-Path $Destination) {
        Remove-Item $Destination -Recurse -Force
        Write-Host "Removed $Destination"
    } else {
        Write-Host "Nothing installed at $Destination"
    }
    return
}

$dll = Join-Path $root "build\spacebeam.dll"
if (-not (Test-Path $dll)) { throw "Build first: .\build.ps1" }

if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    Write-Warning "OBS is running. The DLL cannot be replaced while it is loaded."
    Write-Warning "Close OBS, then run this script again."
    exit 1
}

New-Item -ItemType Directory -Force -Path "$Destination\bin\64bit", "$Destination\data\locale", "$Destination\data\shaders" | Out-Null

# Conventional OBS layout: binaries under bin\64bit. Also clear the older
# root-level DLLs, otherwise OBS would find and load the plugin twice.
Remove-Item "$Destination\spacebeam.dll", "$Destination\spacebeam_dock.dll" -Force -ErrorAction SilentlyContinue

Copy-Item $dll "$Destination\bin\64bit\spacebeam.dll" -Force

# Optional: present only when the build had Qt available.
$dockDll = Join-Path $root "build\spacebeam_dock.dll"
if (Test-Path $dockDll) {
    Copy-Item $dockDll "$Destination\bin\64bit\spacebeam_dock.dll" -Force
    Write-Host "Included spacebeam_dock.dll (control panel)."
} else {
    Remove-Item (Join-Path $Destination "bin\64bit\spacebeam_dock.dll") -Force -ErrorAction SilentlyContinue
    Write-Host "No dock build - installing without the control panel."
}

Copy-Item (Join-Path $root "data\shaders\spacebeam.effect") "$Destination\data\shaders\spacebeam.effect" -Force
Copy-Item (Join-Path $root "data\locale\en-US.ini") "$Destination\data\locale\en-US.ini" -Force

$manifest = @"
{
    "display_name": "SpaceBeam",
    "id": "3f0b1e52-9c74-4a6b-8f21-7d2c5e90b4a1",
    "version": "$version",
    "authors": ["Calmy Jane (original app)", "OBS port"],
    "name": "spacebeam",
    "description": "Real-time kaleidoscope / Droste / tunnel / swirl visual synthesizer ported from SpaceBeam.",
    "long_description": "Ported from the SpaceBeam Android app. Camera Transform, Color, Edge Detect, Kaleidoscope, Droste spiral, Master Transform, 3D Tunnel, Swirl, plus frame feedback.",
    "urls": {"repository": "https://github.com/CalmyJane/SpaceBeam"},
    "has_banner": false,
    "has_icon": false
}
"@
Set-Content -Path "$Destination\data\manifest.json" -Value $manifest -Encoding UTF8

Write-Host "Installed to $Destination"
Write-Host "Start OBS and add a 'SpaceBeam' filter to any source."
