# Publish a release from a local build, using your own gh credentials.
#
#   .\scripts\release.ps1                 # build everything, then publish
#   .\scripts\release.ps1 -Draft          # leave it unpublished for review
#   .\scripts\release.ps1 -NoBuild        # reuse dist\ as-is
#
# Why this exists: the release workflow creates a draft from CI, but the token
# GitHub hands the workflow has been refused release creation on some runs
# ("Resource not accessible by integration"). The workflow now treats that as
# non-fatal, and this script is the reliable path - it publishes with the same
# account that pushed the tag.

param(
    [switch]$Draft,
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $version = (Get-Content VERSION -Raw).Trim()
    $tag = "v$version"

    $status = (git status --porcelain)
    if ($status) { throw "Working tree is dirty; commit before releasing." }

    if (-not $NoBuild) {
        & .\build.ps1
        if ($LASTEXITCODE -ne 0) { throw "build failed" }
        & .\package.ps1 -NoBuild
        & .\tests\verify-package.ps1
        & .\installer.ps1 -NoBuild
    }

    # Windows assets always come from this script. Linux and macOS archives are
    # picked up too when they have been built here (a Docker/CI build dropped
    # into dist\), so a release never advertises a platform it has no file for.
    $assets = @(
        "dist\spacebeam-$version-windows-x64.zip",
        "dist\spacebeam-$version-windows-x64.zip.sha256",
        "dist\SpaceBeam-$version-windows-x64-setup.exe",
        "dist\SpaceBeam-$version-windows-x64-setup.exe.sha256"
    )
    $unix = Get-ChildItem "dist\spacebeam-$version-linux-*.zip*", `
                           "dist\spacebeam-$version-macos-*.zip*" `
                           -ErrorAction SilentlyContinue
    if ($unix) {
        $assets += $unix.FullName
    } else {
        Write-Host "note: no Linux/macOS archive in dist\ - publishing Windows assets only"
    }
    foreach ($a in $assets) {
        if (-not (Test-Path $a)) { throw "missing asset: $a" }
    }

    # Tag the current commit if it is not tagged yet.
    $existing = git tag --list $tag
    if (-not $existing) {
        git tag $tag
        git push origin $tag
    }

    $flags = @()
    if ($Draft) { $flags += '--draft' }

    & gh release create $tag @assets `
        --title "SpaceBeam for OBS $version" `
        --notes-file CHANGELOG.md `
        --target (git rev-parse HEAD) `
        @flags
    if ($LASTEXITCODE -ne 0) { throw "gh release create failed" }

    Write-Host ""
    Write-Host "Released $tag"
    & gh release view $tag --json url --jq .url
}
finally {
    Pop-Location
}
