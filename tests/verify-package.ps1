# Verifies a packaged release zip before it is published.
#   .\tests\verify-package.ps1 [-Zip dist\spacebeam-1.0.1.zip]
#
# Checks the things that have actually broken before:
#   - the plugin folder was missing from the zip root, so extracting into
#     plugins\ left a bare DLL that OBS never loads
#   - the dock DLL was left out of the archive
#   - the filter DLL picked up a Qt dependency, which would break the whole
#     plugin whenever OBS shipped a different Qt
#   - the manifest version drifted from the VERSION file

param(
    [string]$Zip
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()

if (-not $Zip) {
    $Zip = Get-ChildItem (Join-Path $root 'dist\*.zip') |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Zip -or -not (Test-Path $Zip)) { throw "No package found. Run .\package.ps1 first." }

$failures = @()
function Check([bool]$ok, [string]$what) {
    if ($ok) { Write-Host "  ok   $what" }
    else { Write-Host "  FAIL $what"; $script:failures += $what }
}

Write-Host "Verifying $(Split-Path $Zip -Leaf)"

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path $Zip))
try {
    $names = $archive.Entries | ForEach-Object { $_.FullName -replace '\\', '/' }

    # A top-level folder is required: OBS only scans plugins/<name>/.
    Check ($names -contains 'spacebeam/spacebeam.dll') 'spacebeam/spacebeam.dll is at the plugin root'
    Check ($names -contains 'spacebeam/data/manifest.json') 'manifest.json is present'
    Check ($names -contains 'spacebeam/data/shaders/spacebeam.effect') 'shader effect is present'
    Check ($names -contains 'spacebeam/data/locale/en-US.ini') 'locale file is present'
    Check (-not ($names | Where-Object { $_ -match '^[^/]+\.dll$' })) 'no DLLs loose at the zip root'

    # The dock is optional, but if the release was built with it, ship it.
    $dockBuilt = Test-Path (Join-Path $root 'build\spacebeam_dock.dll')
    if ($dockBuilt) {
        Check ($names -contains 'spacebeam/spacebeam_dock.dll') 'spacebeam_dock.dll is included'
    }

    $manifestEntry = $archive.Entries | Where-Object { $_.FullName -like '*manifest.json' }
    if ($manifestEntry) {
        $reader = New-Object System.IO.StreamReader($manifestEntry.Open())
        $manifest = $reader.ReadToEnd() | ConvertFrom-Json
        $reader.Close()
        Check ($manifest.version -eq $version) "manifest version matches VERSION ($version)"
    }

    # The filter must not import Qt; only the dock DLL may.
    $filterEntry = $archive.Entries | Where-Object { $_.FullName -like '*spacebeam.dll' }
    if ($filterEntry) {
        $stream = $filterEntry.Open()
        $buffer = New-Object 'byte[]' ([int]$stream.Length)
        [void]$stream.Read($buffer, 0, $buffer.Length)
        $stream.Close()
        $text = [System.Text.Encoding]::ASCII.GetString($buffer)
        Check (-not $text.Contains('Qt6Widgets.dll')) 'filter DLL has no Qt dependency'
    }
}
finally {
    $archive.Dispose()
}

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "$($failures.Count) check(s) failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Package looks good." -ForegroundColor Green
