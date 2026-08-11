[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$TestRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
. (Join-Path $sourceRoot "scripts\release-provenance.ps1")
$root = [System.IO.Path]::GetFullPath($TestRoot)
if (Test-Path -LiteralPath $root) {
    throw "Installer fail-closed smoke target already exists: $root"
}
New-Item -ItemType Directory -Path $root | Out-Null
$sourceCommit = Get-PdwExactCleanGitHead $sourceRoot "installer fail-closed smoke"

foreach ($fixture in @(
    @{ Architecture = "Win32"; Directory = (Join-Path $root "Win32") },
    @{ Architecture = "x64"; Directory = (Join-Path $root "x64") }
)) {
    New-Item -ItemType Directory -Path $fixture.Directory | Out-Null
    [System.IO.File]::WriteAllText(
        (Join-Path $fixture.Directory "PDW_BUILD_COMMIT.txt"),
        "commit=$sourceCommit`nstate=clean`n",
        [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllBytes(
        (Join-Path $fixture.Directory "payload.bin"),
        [byte[]](0x50, 0x44, 0x57, 0x00))
    Write-PdwInstallerInputManifest $fixture.Directory $fixture.Architecture $sourceCommit
}

$fakeCompiler = Join-Path $root "fake-iscc.ps1"
$fakeCompilerText = @'
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CompilerArguments
)
$outputArgument = $CompilerArguments | Where-Object {
    $_.StartsWith('/DInstallerOutput=', [System.StringComparison]::OrdinalIgnoreCase)
} | Select-Object -First 1
$win32Argument = $CompilerArguments | Where-Object {
    $_.StartsWith('/DWin32Application=', [System.StringComparison]::OrdinalIgnoreCase)
} | Select-Object -First 1
if (-not $outputArgument -or -not $win32Argument) {
    throw 'Fake compiler did not receive required Inno defines.'
}
$output = $outputArgument.Substring('/DInstallerOutput='.Length)
$win32 = $win32Argument.Substring('/DWin32Application='.Length)
New-Item -ItemType Directory -Path $output -Force | Out-Null
[System.IO.File]::WriteAllBytes(
    (Join-Path $output 'PDW-v5.5.1-2026-Release-Setup.exe'),
    [byte[]](0x4d, 0x5a, 0x00, 0x00))
if ($env:PDW_FAKE_COMPILER_MUTATE_INPUT -eq '1') {
    [System.IO.File]::AppendAllText(
        (Join-Path $win32 'payload.bin'),
        'postcompile mutation',
        [System.Text.UTF8Encoding]::new($false))
}
$global:LASTEXITCODE = 0
'@
[System.IO.File]::WriteAllText($fakeCompiler, $fakeCompilerText,
    [System.Text.UTF8Encoding]::new($false))

$outputRoot = Join-Path $root "output"
$failedClosed = $false
$env:PDW_FAKE_COMPILER_MUTATE_INPUT = '1'
try {
    & (Join-Path $sourceRoot "scripts\build-installer.ps1") `
        -Win32ApplicationDirectory (Join-Path $root "Win32") `
        -X64ApplicationDirectory (Join-Path $root "x64") `
        -OutputDirectory $outputRoot `
        -InnoCompiler $fakeCompiler
}
catch {
    if ($_.Exception.Message -match 'SHA-256 validation') {
        $failedClosed = $true
    }
    else {
        throw
    }
}
finally {
    Remove-Item Env:PDW_FAKE_COMPILER_MUTATE_INPUT -ErrorAction SilentlyContinue
}
if (-not $failedClosed) {
    throw "Setup build accepted a postcompile installer-input mutation."
}

$publicSet = Join-Path $outputRoot "PDW-v5.5.1-2026-Release-Setup-package"
$publicSetup = Join-Path $publicSet "PDW-v5.5.1-2026-Release-Setup.exe"
if ((Test-Path -LiteralPath $publicSetup) -or
    (Test-Path -LiteralPath "$publicSetup.sha256")) {
    throw "Failed Setup validation left a public candidate or checksum behind."
}
$temporaryOutputs = @(Get-ChildItem -LiteralPath $outputRoot -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'pdw-installer-*' })
if ($temporaryOutputs.Count -ne 0) {
    throw "Failed Setup validation left temporary compiler inputs or outputs behind."
}

# Restore the exact fixture and prove a mutation after Authenticode policy is
# detected before either public output is created.
$win32Fixture = Join-Path $root "Win32"
[System.IO.File]::WriteAllBytes((Join-Path $win32Fixture "payload.bin"),
    [byte[]](0x50, 0x44, 0x57, 0x00))
Remove-Item -LiteralPath (Join-Path $win32Fixture $script:PdwInstallerInputManifestName)
Write-PdwInstallerInputManifest $win32Fixture "Win32" $sourceCommit
$policyMutationRejected = $false
$env:PDW_RELEASE_TEST_MUTATE_SETUP_AFTER_POLICY = '1'
try {
    & (Join-Path $sourceRoot "scripts\build-installer.ps1") `
        -Win32ApplicationDirectory $win32Fixture `
        -X64ApplicationDirectory (Join-Path $root "x64") `
        -OutputDirectory $outputRoot `
        -InnoCompiler $fakeCompiler
}
catch {
    if ($_.Exception.Message -match 'Authenticode policy') {
        $policyMutationRejected = $true
    }
    else {
        throw
    }
}
finally {
    Remove-Item Env:PDW_RELEASE_TEST_MUTATE_SETUP_AFTER_POLICY -ErrorAction SilentlyContinue
}
if (-not $policyMutationRejected -or
    (Test-Path -LiteralPath $publicSetup) -or
    (Test-Path -LiteralPath "$publicSetup.sha256")) {
    throw "A post-policy Setup mutation was published or left a checksum behind."
}

# Deterministically occupy the final Setup name after preflight. The operator
# file must survive unchanged and no checksum may be published.
$destinationOccupancyRejected = $false
$env:PDW_RELEASE_TEST_OCCUPY_SETUP_DESTINATION = '1'
try {
    & (Join-Path $sourceRoot "scripts\build-installer.ps1") `
        -Win32ApplicationDirectory $win32Fixture `
        -X64ApplicationDirectory (Join-Path $root "x64") `
        -OutputDirectory $outputRoot `
        -InnoCompiler $fakeCompiler
}
catch {
    if ($_.Exception.Message -match 'became occupied') {
        $destinationOccupancyRejected = $true
    }
    else {
        throw
    }
}
finally {
    Remove-Item Env:PDW_RELEASE_TEST_OCCUPY_SETUP_DESTINATION -ErrorAction SilentlyContinue
}
if (-not $destinationOccupancyRejected -or
    [System.IO.File]::ReadAllText($publicSetup) -cne "operator-owned setup`n" -or
    (Test-Path -LiteralPath "$publicSetup.sha256")) {
    throw "Setup destination occupancy overwrote the operator file or published a checksum."
}

[pscustomobject]@{
    CompilerSucceeded = "Passed"
    PostcompileMutationRejected = "Passed"
    PublicSetupAbsent = "Passed"
    TemporaryOutputsRemoved = "Passed"
    PostPolicyMutationRejected = "Passed"
    DestinationOccupancyRejected = "Passed"
}
