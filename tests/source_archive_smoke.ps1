[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageZip,
    [Parameter(Mandatory = $true)]
    [string]$TestRoot,
    [Parameter(Mandatory = $true)]
    [ValidateSet("Win32", "x64")]
    [string]$Architecture,
    [Parameter(Mandatory = $true)]
    [string]$DependencyRoot,
    [string]$Generator = "Visual Studio 18 2026",
    [string]$Toolset = "v145",
    [string]$CMakeCommand = "cmake"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$packagePath = (Resolve-Path -LiteralPath $PackageZip).Path
$dependencyPath = (Resolve-Path -LiteralPath $DependencyRoot).Path
$root = [System.IO.Path]::GetFullPath($TestRoot)
if (Test-Path -LiteralPath $root) {
    throw "Source-archive smoke target already exists: $root"
}
New-Item -ItemType Directory -Path $root | Out-Null
$root = (Resolve-Path -LiteralPath $root).Path
$cmake = (Get-Command $CMakeCommand -ErrorAction Stop).Source

$expanded = Join-Path $root "expanded"
Expand-Archive -LiteralPath $packagePath -DestinationPath $expanded
$commitMarkers = @(Get-ChildItem -LiteralPath $expanded -Recurse -Force -File `
    -Filter SOURCE_COMMIT.txt)
if ($commitMarkers.Count -ne 1) {
    throw "Portable package must contain exactly one SOURCE_COMMIT.txt."
}
$packageRoot = Split-Path -Parent $commitMarkers[0].FullName
$source = Join-Path $packageRoot "Source"
if (-not (Test-Path -LiteralPath $source -PathType Container) -or
    (Test-Path -LiteralPath (Join-Path $source ".git"))) {
    throw "Portable Source must be an extracted Git archive without .git."
}
$expectedCommit = (Get-Content -LiteralPath $commitMarkers[0].FullName -Raw).Trim().ToLowerInvariant()
if ($expectedCommit -notmatch '^[0-9a-f]{40}$') {
    throw "Portable SOURCE_COMMIT.txt is malformed."
}

function Invoke-Validator([string]$SourceDirectory, [bool]$ShouldPass, [string]$Label) {
    $validator = Join-Path $SourceDirectory "scripts\validate-source-archive.cmake"
    & $cmake "-DPDW_SOURCE_DIRECTORY=$SourceDirectory" -P $validator
    $exitCode = $LASTEXITCODE
    if ($ShouldPass -and $exitCode -ne 0) {
        throw "Release-source validator rejected the intact $Label tree."
    }
    if (-not $ShouldPass -and $exitCode -eq 0) {
        throw "Release-source validator accepted the $Label tree."
    }
}

Invoke-Validator $source $true "archive"

foreach ($negative in @(
    @{ Name = "tampered"; Action = "tamper" },
    @{ Name = "missing"; Action = "remove" },
    @{ Name = "extra"; Action = "extra" }
)) {
    $negativeRoot = Join-Path $root $negative.Name
    Copy-Item -LiteralPath $source -Destination $negativeRoot -Recurse
    switch ($negative.Action) {
        "tamper" {
            Add-Content -LiteralPath (Join-Path $negativeRoot "Readme") `
                -Value "synthetic provenance tamper" -Encoding UTF8
        }
        "remove" {
            Remove-Item -LiteralPath (Join-Path $negativeRoot "Readme")
        }
        "extra" {
            Set-Content -LiteralPath (Join-Path $negativeRoot "unlisted-source-file.txt") `
                -Value "synthetic unlisted file" -Encoding Ascii
        }
    }
    Invoke-Validator $negativeRoot $false $negative.Name
}

$build = Join-Path $root "build"
& $cmake -S $source -B $build -G $Generator -A $Architecture -T $Toolset `
    "-DPDW_DEPENDENCY_ROOT=$dependencyPath" -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw "Extracted Source configure failed." }
& $cmake --build $build --config Release --target PDW --parallel
if ($LASTEXITCODE -ne 0) { throw "Extracted Source PDW build failed." }

$releaseDirectory = Join-Path $build "Release"
$buildMarker = Join-Path $releaseDirectory "PDW_BUILD_COMMIT.txt"
if (-not (Test-Path -LiteralPath $buildMarker -PathType Leaf)) {
    throw "Extracted Source build did not emit PDW_BUILD_COMMIT.txt."
}
$expectedBuildMarker = "commit=$expectedCommit`nstate=clean`n"
$actualBuildMarker = (Get-Content -LiteralPath $buildMarker -Raw).
    Replace("`r`n", "`n").Replace("`r", "`n")
if ($actualBuildMarker -cne $expectedBuildMarker) {
    throw "Extracted Source build marker does not match its validated clean archive commit."
}

# Regress the two-phase target-marker contract without disturbing the accepted
# build: PRE_LINK must invalidate the marker that sits beside the executable,
# and a subsequent validator failure must not be able to promote it.
$staleMarker = Join-Path $root "stale-target-PDW_BUILD_COMMIT.txt"
[System.IO.File]::WriteAllText($staleMarker, $expectedBuildMarker,
    [System.Text.UTF8Encoding]::new($false))
& $cmake "-DPDW_PROVENANCE_OUTPUT=$staleMarker" `
    "-DPDW_PROVENANCE_COMMIT=$expectedCommit" `
    -P (Join-Path $source "scripts\invalidate-build-provenance.cmake")
if ($LASTEXITCODE -ne 0) { throw "Target-marker invalidation regression failed." }
$dirtyMarker = (Get-Content -LiteralPath $staleMarker -Raw).
    Replace("`r`n", "`n").Replace("`r", "`n")
if ($dirtyMarker -cne "commit=$expectedCommit`nstate=dirty`n") {
    throw "PRE_LINK provenance invalidation did not replace the stale clean marker."
}
$failedMetadata = Join-Path $root "failed-build-metadata.txt"
$tamperedSource = Join-Path $root "tampered"
& $cmake "-DPDW_SOURCE_DIRECTORY=$tamperedSource" `
    "-DPDW_PROVENANCE_OUTPUT=$failedMetadata" `
    "-DPDW_CONFIGURED_COMMIT=$expectedCommit" `
    -DPDW_CONFIGURED_STATE=clean `
    -DPDW_SOURCE_PROVENANCE_MODE=archive `
    -P (Join-Path $source "scripts\write-build-provenance.cmake")
if ($LASTEXITCODE -eq 0) {
    throw "Build-time provenance regression accepted a tampered archive."
}
$dirtyMarkerAfterFailure = (Get-Content -LiteralPath $staleMarker -Raw).
    Replace("`r`n", "`n").Replace("`r", "`n")
if ($dirtyMarkerAfterFailure -cne "commit=$expectedCommit`nstate=dirty`n") {
    throw "Failed build-time validation restored a stale clean target marker."
}

[pscustomobject]@{
    Architecture = $Architecture
    Commit = $expectedCommit
    Configure = "Passed"
    Build = "Passed"
    TamperRejected = "Passed"
    MissingFileRejected = "Passed"
    ExtraFileRejected = "Passed"
    StaleMarkerFailClosed = "Passed"
}

# The final provenance check intentionally runs CMake against a tampered tree.
# All assertions above have passed, so do not leak that expected native failure
# through PowerShell as the overall script result (notably in GitHub Actions).
$global:LASTEXITCODE = 0
