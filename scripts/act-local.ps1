# Run the GitHub Actions workflow locally with act.
#
#   .\scripts\act-local.ps1              # the fast, no-Qt job
#   .\scripts\act-local.ps1 -Job full    # the Qt/dock job
#   .\scripts\act-local.ps1 -List        # just show the jobs
#
# Windows jobs cannot run in act's default Linux containers, so this uses
# "self-hosted" mode: the steps run directly on this machine. That is the point
# - OBS Studio and the toolchain are already here - but it also means the run is
# only as faithful as the local install. Steps that a hosted runner provides
# (installing OBS, uploading artifacts) are skipped via `if: ${{ !env.ACT }}`.

param(
    [string]$Job = 'filter-only',
    [switch]$List
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$act = Get-Command act -ErrorAction SilentlyContinue
if (-not $act) {
    $pkg = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\nektos.act_Microsoft.Winget.Source_8wekyb3d8bbwe\act.exe'
    if (Test-Path $pkg) {
        $act = [pscustomobject]@{ Source = $pkg }
    } else {
        throw "act not found. Install it with: winget install --id nektos.act"
    }
}

Push-Location $root
try {
    if ($List) {
        & $act.Source -l -P windows-latest=-self-hosted --trust-project-actrc
        return
    }

    if (Get-Process obs64 -ErrorAction SilentlyContinue) {
        Write-Warning "OBS is running; build.ps1 needs to replace build\spacebeam.dll, which is fine, but the installed copy cannot change. Close OBS if the job touches it."
    }

    & $act.Source -j $Job `
        -W .github/workflows/build.yml `
        -P windows-latest=-self-hosted `
        --trust-project-actrc
}
finally {
    Pop-Location
}
