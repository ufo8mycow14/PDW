[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$TestRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
. (Join-Path $sourceRoot "scripts\release-provenance.ps1")

foreach ($privateName in @(
    "PDW.INI.pre-adelaide-flex-20260811.bak",
    "PDW.INI.pre-adelaide-flex-20260811.bak.2",
    "PDW.INI.pre-adelaide-flex-unverified-retained",
    "PAP1234.tmp",
    "PDS5678.tmp",
    "PDW.INI.operator-swap",
    "PDW.INI.operator-swap.test",
    "PDW.INI.operator-swap-2"
)) {
    if (-not (Test-PdwPrivateTransactionArtifactName $privateName)) {
        throw "Private transaction-artifact predicate failed to reject $privateName."
    }
}
foreach ($publicName in @(
    "PDW-Adelaide-FLEX.INI",
    "PDW.INI.example",
    "operator-swap.txt",
    "PAX1234.tmp"
)) {
    if (Test-PdwPrivateTransactionArtifactName $publicName) {
        throw "Private transaction-artifact predicate rejected $publicName."
    }
}
foreach ($sensitiveLine in @(
    "Password=synthetic-only",
    "Token=synthetic-only",
    "DeviceEndpointId=utf8-hex:73796e746865746963"
)) {
    if (-not (Get-PdwSensitiveIniFieldName $sensitiveLine)) {
        throw "Sensitive INI-field predicate failed to reject $sensitiveLine."
    }
}
foreach ($safeLine in @(
    "Password=",
    "DeviceEndpointId=",
    "DeviceFriendlyName=CABLE Output (VB-Audio Virtual Cable)"
)) {
    if (Get-PdwSensitiveIniFieldName $safeLine) {
        throw "Sensitive INI-field predicate rejected $safeLine."
    }
}

$root = [System.IO.Path]::GetFullPath($TestRoot)
if (Test-Path -LiteralPath $root) {
    throw "Release-provenance smoke target already exists: $root"
}
New-Item -ItemType Directory -Path $root | Out-Null
$repository = Join-Path $root "repository"
$snapshot = Join-Path $root "snapshot"
$installerInput = Join-Path $root "installer-input"
$hashFixture = Join-Path $root "hash-fixture"
$hashArchive = Join-Path $root "hash-fixture.zip"
$hashExpanded = Join-Path $root "hash-expanded"
$directoryMoveCandidate = Join-Path $root "directory-move-candidate"
$directoryMoveOccupied = Join-Path $root "directory-move-occupied"
$fileMoveCandidate = Join-Path $root "file-move-candidate.zip"
$fileMoveOccupied = Join-Path $root "file-move-occupied.zip"
New-Item -ItemType Directory -Path $repository | Out-Null

New-Item -ItemType Directory -Path $hashFixture | Out-Null
[System.IO.File]::WriteAllText((Join-Path $hashFixture "payload.txt"),
    "synthetic package payload`n", [System.Text.UTF8Encoding]::new($false))
Write-PdwDirectoryHashManifest $hashFixture
Assert-PdwDirectoryHashManifest $hashFixture
Compress-Archive -LiteralPath $hashFixture -DestinationPath $hashArchive
Expand-Archive -LiteralPath $hashArchive -DestinationPath $hashExpanded
$expandedHashFixture = Join-Path $hashExpanded "hash-fixture"
Assert-PdwDirectoryHashManifest $expandedHashFixture

[System.IO.File]::AppendAllText((Join-Path $hashFixture "payload.txt"),
    "tampered`n", [System.Text.UTF8Encoding]::new($false))
$directoryManifestTamperRejected = $false
try {
    Assert-PdwDirectoryHashManifest $hashFixture
}
catch {
    $directoryManifestTamperRejected = $true
}
if (-not $directoryManifestTamperRejected) {
    throw "The directory hash manifest accepted a modified package file."
}

[System.IO.File]::WriteAllText((Join-Path $expandedHashFixture "unlisted.txt"),
    "extra`n", [System.Text.UTF8Encoding]::new($false))
$expandedZipExtraRejected = $false
try {
    Assert-PdwDirectoryHashManifest $expandedHashFixture
}
catch {
    $expandedZipExtraRejected = $true
}
if (-not $expandedZipExtraRejected) {
    throw "Expanded ZIP validation accepted an unlisted package file."
}

# A destination created after the package preflight must never be merged into,
# overwritten, or deleted. The private candidates must remain available for a
# later safe retry.
New-Item -ItemType Directory -Path $directoryMoveCandidate | Out-Null
[System.IO.File]::WriteAllText((Join-Path $directoryMoveCandidate "candidate.txt"),
    "candidate`n", [System.Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path $directoryMoveOccupied | Out-Null
$operatorDirectoryFile = Join-Path $directoryMoveOccupied "operator.txt"
[System.IO.File]::WriteAllText($operatorDirectoryFile,
    "operator-owned`n", [System.Text.UTF8Encoding]::new($false))
$directoryOccupancyRejected = $false
try {
    Move-PdwDirectoryNoReplace $directoryMoveCandidate $directoryMoveOccupied
}
catch {
    $directoryOccupancyRejected = $true
}
if (-not $directoryOccupancyRejected -or
    -not (Test-Path -LiteralPath $directoryMoveCandidate -PathType Container) -or
    [System.IO.File]::ReadAllText($operatorDirectoryFile) -cne "operator-owned`n" -or
    (Test-Path -LiteralPath (Join-Path $directoryMoveOccupied "candidate.txt"))) {
    throw "No-replace directory publication altered or merged into an occupied destination."
}

[System.IO.File]::WriteAllText($fileMoveCandidate,
    "candidate zip`n", [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText($fileMoveOccupied,
    "operator zip`n", [System.Text.UTF8Encoding]::new($false))
$fileOccupancyRejected = $false
try {
    Move-PdwFileNoReplace $fileMoveCandidate $fileMoveOccupied
}
catch {
    $fileOccupancyRejected = $true
}
if (-not $fileOccupancyRejected -or
    [System.IO.File]::ReadAllText($fileMoveCandidate) -cne "candidate zip`n" -or
    [System.IO.File]::ReadAllText($fileMoveOccupied) -cne "operator zip`n") {
    throw "No-replace ZIP publication overwrote or removed an occupied destination."
}

& git -C $repository init --quiet
if ($LASTEXITCODE -ne 0) { throw "Unable to initialize provenance smoke repository." }
& git -C $repository config user.name "PDW provenance smoke"
& git -C $repository config user.email "pdw-provenance-smoke@example.invalid"
& git -C $repository config core.autocrlf false
$committedText = "committed release input`n"
[System.IO.File]::WriteAllText((Join-Path $repository "release-input.txt"),
    $committedText, [System.Text.UTF8Encoding]::new($false))
& git -C $repository add -- release-input.txt
& git -C $repository commit --quiet -m "provenance smoke fixture"
if ($LASTEXITCODE -ne 0) { throw "Unable to commit provenance smoke fixture." }

$commit = Get-PdwExactCleanGitHead $repository "provenance smoke preflight"
New-PdwGitCommitSnapshot $repository $commit $snapshot
$snapshottedText = [System.IO.File]::ReadAllText(
    (Join-Path $snapshot "release-input.txt"))
if ($snapshottedText -cne $committedText) {
    throw "The immutable Git snapshot does not contain the committed bytes."
}

# Deterministically inject a post-preflight working-tree mutation. The snapshot
# must remain the committed input and the final clean-HEAD gate must reject it.
[System.IO.File]::WriteAllText((Join-Path $repository "release-input.txt"),
    "injected after preflight`n", [System.Text.UTF8Encoding]::new($false))
if ([System.IO.File]::ReadAllText((Join-Path $snapshot "release-input.txt")) -cne
    $committedText) {
    throw "A post-preflight working-tree change altered the immutable snapshot."
}
$mutationRejected = $false
try {
    Assert-PdwExactCleanGitHead $repository $commit "mutation regression"
}
catch {
    $mutationRejected = $true
}
if (-not $mutationRejected) {
    throw "The final clean-HEAD gate accepted a post-preflight source mutation."
}
[System.IO.File]::WriteAllText((Join-Path $repository "release-input.txt"),
    $committedText, [System.Text.UTF8Encoding]::new($false))
Assert-PdwExactCleanGitHead $repository $commit "restored mutation regression"

New-Item -ItemType Directory -Path (Join-Path $installerInput "docs") -Force |
    Out-Null
[System.IO.File]::WriteAllText((Join-Path $installerInput "PDW_BUILD_COMMIT.txt"),
    "commit=$commit`nstate=clean`n", [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllBytes((Join-Path $installerInput "PDW.exe"),
    [byte[]](0x50, 0x44, 0x57, 0x00))
[System.IO.File]::WriteAllText((Join-Path $installerInput "docs\Readme"),
    "installer input`n", [System.Text.UTF8Encoding]::new($false))
Write-PdwInstallerInputManifest $installerInput "x64" $commit
Assert-PdwInstallerInputManifest $installerInput "x64" $commit

[System.IO.File]::AppendAllText((Join-Path $installerInput "docs\Readme"),
    "tampered`n", [System.Text.UTF8Encoding]::new($false))
$tamperRejected = $false
try {
    Assert-PdwInstallerInputManifest $installerInput "x64" $commit
}
catch {
    $tamperRejected = $true
}
if (-not $tamperRejected) {
    throw "The installer-input manifest accepted a modified staged file."
}

[System.IO.File]::WriteAllText((Join-Path $installerInput "docs\Readme"),
    "installer input`n", [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText((Join-Path $installerInput "unlisted.tmp"),
    "unlisted`n", [System.Text.UTF8Encoding]::new($false))
$extraRejected = $false
try {
    Assert-PdwInstallerInputManifest $installerInput "x64" $commit
}
catch {
    $extraRejected = $true
}
if (-not $extraRejected) {
    throw "The installer-input manifest accepted an unlisted staged file."
}

[pscustomobject]@{
    Commit = $commit
    ImmutableSnapshot = "Passed"
    PostPreflightMutationRejected = "Passed"
    ManifestTamperRejected = "Passed"
    ManifestExtraRejected = "Passed"
    PrivateTransactionArtifactNames = "Passed"
    SensitiveIniFields = "Passed"
    DirectoryManifestTamperRejected = "Passed"
    ExpandedZipExtraRejected = "Passed"
    DirectoryDestinationOccupancyRejected = "Passed"
    ZipDestinationOccupancyRejected = "Passed"
}
