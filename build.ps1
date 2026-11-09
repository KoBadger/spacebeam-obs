# Build SpaceBeam for OBS against the installed OBS Studio.
#   .\build.ps1                      normal build
#   .\build.ps1 -RegenerateImportLib rebuild deps/obs.lib from obs.dll
#   .\build.ps1 -ObsRoot "D:\obs"    non-default install location

param(
    [switch]$RegenerateImportLib,
    [string]$ObsRoot = "C:\Program Files\obs-studio",
    [switch]$NoDock
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere not found. Install Visual Studio with the C++ workload." }

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "No Visual Studio C++ toolset found." }

$vcvars  = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
$cmake   = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja   = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$msvcBin = Get-ChildItem (Join-Path $vsPath "VC\Tools\MSVC") -Directory |
           Sort-Object Name -Descending | Select-Object -First 1

$dumpbin = Join-Path $msvcBin.FullName "bin\Hostx64\x64\dumpbin.exe"
$libexe  = Join-Path $msvcBin.FullName "bin\Hostx64\x64\lib.exe"

foreach ($f in @($vcvars, $cmake, $ninja, $dumpbin, $libexe)) {
    if (-not (Test-Path $f)) { throw "Missing tool: $f" }
}

$obsDll = Join-Path $ObsRoot "bin\64bit\obs.dll"
if (-not (Test-Path $obsDll)) { throw "obs.dll not found at $obsDll" }

New-Item -ItemType Directory -Force -Path (Join-Path $root "deps\lib") | Out-Null

# Build an import library from a DLL's export table. OBS ships no .lib files,
# so this is the only way to link against it.
function New-ImportLib([string]$DllPath, [string]$LibName) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($DllPath)
    Write-Host "Regenerating $LibName from $DllPath ..."

    $dump = & $dumpbin /exports $DllPath
    $names = foreach ($line in $dump) {
        if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)') { $matches[1] }
    }
    $names = $names | Where-Object { $_ -and $_ -ne 'ordinal' } | Select-Object -Unique
    if (-not $names) { throw "Could not parse exports from $DllPath" }

    $def = "LIBRARY $base`r`nEXPORTS`r`n" + (($names | ForEach-Object { "  $_" }) -join "`r`n") + "`r`n"
    $defPath = Join-Path $root "deps\$base.def"
    $libPath = Join-Path $root "deps\lib\$LibName"
    Set-Content -Path $defPath -Value $def -Encoding ASCII

    & $libexe "/def:$defPath" "/out:$libPath" /machine:x64 /nologo | Out-Null
    if (-not (Test-Path $libPath)) { throw "lib.exe did not produce $libPath" }
    Write-Host ("  {0} exports -> deps\lib\{1}" -f $names.Count, $LibName)
}

if ($RegenerateImportLib -or -not (Test-Path (Join-Path $root "deps\lib\obs.lib"))) {
    New-ImportLib $obsDll "obs.lib"
}

# obs-frontend-api is needed by the dock panel.
$frontendDll = Join-Path $ObsRoot "bin\64bit\obs-frontend-api.dll"
if (Test-Path $frontendDll) {
    if ($RegenerateImportLib -or -not (Test-Path (Join-Path $root "deps\lib\obs-frontend-api.lib"))) {
        New-ImportLib $frontendDll "obs-frontend-api.lib"
    }
} else {
    Write-Host "obs-frontend-api.dll not found - dock panel will be disabled."
}

Write-Host "Configuring ..."
& cmd /c "`"$vcvars`" >nul 2>&1 && `"$cmake`" -S `"$root`" -B `"$root\build`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DOBS_ROOT=`"$ObsRoot`" -DSB_NO_DOCK=$($NoDock.IsPresent.ToString().ToUpper())"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "Building ..."
& cmd /c "`"$vcvars`" >nul 2>&1 && `"$cmake`" --build `"$root\build`""
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host ""
Write-Host "Built: $root\build\spacebeam.dll"
Write-Host "Install with: .\install.ps1"
