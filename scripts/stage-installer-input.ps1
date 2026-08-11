[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Win32", "x64")]
    [string]$Architecture,
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [Parameter(Mandatory = $true)]
    [string]$Destination
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "release-provenance.ps1")

function Get-PeMachine([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        if ($stream.Length -lt 64) { throw "File is too small to be a Windows PE file: $Path" }
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "File has no DOS header: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset + 6 -gt $stream.Length) {
            throw "File has an invalid PE offset: $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "File has no PE signature: $Path" }
        return $reader.ReadUInt16()
    }
    finally {
        $stream.Dispose()
    }
}

$appLocalRuntimeAllowlist = @(
    "concrt140.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll"
)

function Get-RequiredAppLocalRuntimeNames([string]$TargetArchitecture) {
    $names = @($appLocalRuntimeAllowlist | Where-Object { $_ -ne "vcruntime140_1.dll" })
    if ($TargetArchitecture -eq "x64") {
        $names += "vcruntime140_1.dll"
    }
    return $names
}

function Assert-AppLocalRuntimeSet(
    [string]$Directory,
    [string]$TargetArchitecture,
    [uint16]$ExpectedMachine
) {
    $requiredNames = @(Get-RequiredAppLocalRuntimeNames $TargetArchitecture)
    $rootDlls = @(Get-ChildItem -LiteralPath $Directory -File -Filter *.dll)
    $runtimeLike = @($rootDlls | Where-Object {
        $_.Name -match '^(?i:(?:concrt|msvcp|vccorlib|vcruntime)[0-9][a-z0-9_]*|ucrtbase|api-ms-win-crt-[a-z0-9_-]+)\.dll$'
    })
    $unexpected = @($runtimeLike | Where-Object {
        $appLocalRuntimeAllowlist -notcontains $_.Name
    })
    if ($unexpected) {
        throw "Build output contains an app-local runtime outside the explicit allowlist: $($unexpected.Name -join ', ')"
    }

    $selected = @($runtimeLike | Where-Object {
        $appLocalRuntimeAllowlist -contains $_.Name
    })
    $missing = @($requiredNames | Where-Object { $selected.Name -notcontains $_ })
    $extra = @($selected | Where-Object { $requiredNames -notcontains $_.Name })
    if ($missing -or $extra) {
        throw ("Build output does not contain the exact reviewed {0} app-local runtime set. Missing: [{1}]. Extra: [{2}]." -f
            $TargetArchitecture, ($missing -join ', '), ($extra.Name -join ', '))
    }

    $validated = @()
    foreach ($name in $requiredNames) {
        $runtime = $selected | Where-Object Name -IEQ $name | Select-Object -First 1
        $machine = Get-PeMachine $runtime.FullName
        if ($machine -ne $ExpectedMachine) {
            throw ("Runtime {0} does not match {1}: PE machine 0x{2:X4}." -f
                $runtime.Name, $TargetArchitecture, $machine)
        }
        $versionInfo = $runtime.VersionInfo
        if ($versionInfo.FileVersion -notmatch '^14\.5[0-9]\.' -or
            -not [string]::Equals($versionInfo.OriginalFilename, $runtime.Name,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Runtime $($runtime.Name) is not a validated Microsoft VC145 release DLL."
        }
        $validated += $runtime
    }
    return $validated
}

function Assert-BuildCommitMarker([string]$Directory, [string]$ExpectedCommit) {
    $marker = Join-Path $Directory "PDW_BUILD_COMMIT.txt"
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw "Build provenance marker is missing: $marker"
    }
    $expectedText = "commit=$ExpectedCommit`nstate=clean`n"
    $actualText = (Get-Content -LiteralPath $marker -Raw).Replace("`r`n", "`n").Replace("`r", "`n")
    if ($actualText -cne $expectedText) {
        throw "Build provenance does not identify exact clean source commit $ExpectedCommit. Reconfigure and rebuild that clean commit."
    }
    return $marker
}

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = [System.IO.Path]::GetFullPath($BuildDirectory)
$destinationRoot = [System.IO.Path]::GetFullPath($Destination)
if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
    throw "Build directory does not exist: $buildRoot"
}
if (Test-Path -LiteralPath $destinationRoot) {
    throw "Installer input destination already exists: $destinationRoot"
}

& (Join-Path $sourceRoot "scripts\audit-release.ps1")
if ($LASTEXITCODE -ne 0) { throw "Repository release audit failed." }
$sourceCommit = Get-PdwExactCleanGitHead $sourceRoot "installer-input staging"

$parent = Split-Path -Parent $destinationRoot
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent | Out-Null
}
$staging = Join-Path $parent ("pdw-installer-input-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path $staging | Out-Null
    $sourceSnapshot = Join-Path $staging ".source-snapshot"
    New-PdwGitCommitSnapshot $sourceRoot $sourceCommit $sourceSnapshot

    $versionHeader = Get-Content -LiteralPath (Join-Path $sourceSnapshot "Headers\version.h") -Raw
    function Read-StringMacro([string]$Name) {
        $match = [regex]::Match($versionHeader,
            '(?m)^#define ' + [regex]::Escape($Name) + ' "([^"]+)"\r?$')
        if (-not $match.Success) { throw "Unable to read $Name from Headers\version.h." }
        return $match.Groups[1].Value
    }
    $displayName = Read-StringMacro "PDW_DISPLAY_VERSION"
    $productVersion = Read-StringMacro "PDW_VERSION_STRING"
    $resourceVersion = Read-StringMacro "PDW_VERSION_RESOURCE_STRING"
    $executable = Join-Path $buildRoot "$displayName.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Release executable does not exist: $executable"
    }

    $machine = Get-PeMachine $executable
    $expectedMachine = if ($Architecture -eq "x64") { 0x8664 } else { 0x014c }
    if ($machine -ne $expectedMachine) {
        throw ("Executable does not match {0}: PE machine 0x{1:X4}." -f $Architecture, $machine)
    }
    $runtimeFiles = @(Assert-AppLocalRuntimeSet $buildRoot $Architecture $expectedMachine)
    $buildCommitMarker = Assert-BuildCommitMarker $buildRoot $sourceCommit
    $version = (Get-Item -LiteralPath $executable).VersionInfo
    if ($version.FileVersion -ne $resourceVersion -or $version.ProductVersion -ne $productVersion) {
        throw "Executable version metadata does not match $displayName."
    }
    Copy-PdwFileHashStable $executable (Join-Path $staging "$displayName.exe")
    Copy-PdwFileHashStable $buildCommitMarker (Join-Path $staging "PDW_BUILD_COMMIT.txt")
    foreach ($file in @(
        @{ Source = (Join-Path $sourceSnapshot "packaging\PDW.INI"); Name = "PDW.INI" },
        @{ Source = (Join-Path $sourceSnapshot "packaging\PDW-Adelaide-FLEX.INI"); Name = "PDW-Adelaide-FLEX.INI" },
        @{ Source = (Join-Path $sourceSnapshot "packaging\Legacy\base-ids.txt"); Name = "base-ids.txt" },
        @{ Source = (Join-Path $sourceSnapshot "packaging\Legacy\language.df"); Name = "language.df" },
        @{ Source = (Join-Path $sourceSnapshot "packaging\Legacy\PDW.HLP"); Name = "PDW.HLP" },
        @{ Source = (Join-Path $sourceSnapshot "pdw-manual.pdf"); Name = "PDW.pdf" },
        @{ Source = (Join-Path $sourceSnapshot "Readme"); Name = "Readme" },
        @{ Source = (Join-Path $sourceSnapshot "CHANGELOG.md"); Name = "CHANGELOG.md" },
        @{ Source = (Join-Path $sourceSnapshot "License"); Name = "License" },
        @{ Source = (Join-Path $sourceSnapshot "THIRD_PARTY_NOTICES.md"); Name = "THIRD_PARTY_NOTICES.md" }
    )) {
        if (-not (Test-Path -LiteralPath $file.Source -PathType Leaf)) {
            throw "Required installer file is missing: $($file.Source)"
        }
        Copy-Item -LiteralPath $file.Source -Destination (Join-Path $staging $file.Name)
    }
    foreach ($runtime in $runtimeFiles) {
        Copy-PdwFileHashStable $runtime.FullName (Join-Path $staging $runtime.Name)
    }
    Copy-Item -LiteralPath (Join-Path $sourceSnapshot "docs") -Destination $staging -Recurse
    Copy-Item -LiteralPath (Join-Path $sourceSnapshot "Receivers") -Destination $staging -Recurse
    Copy-Item -LiteralPath (Join-Path $sourceSnapshot "packaging\Wavfiles") -Destination $staging -Recurse
    if ($Architecture -eq "x64") {
        $x86Receiver = Join-Path $staging "Receivers\RTL-SDR\rtlsdr.dll"
        if (Test-Path -LiteralPath $x86Receiver -PathType Leaf) {
            Remove-Item -LiteralPath $x86Receiver
        }
    }
    Remove-Item -LiteralPath $sourceSnapshot -Recurse -Force

    $stagedExecutable = Join-Path $staging "$displayName.exe"
    if ((Get-PeMachine $stagedExecutable) -ne $expectedMachine) {
        throw "The copied installer executable changed architecture during staging."
    }
    [void](Assert-AppLocalRuntimeSet $staging $Architecture $expectedMachine)
    [void](Assert-BuildCommitMarker $staging $sourceCommit)
    $stagedVersion = (Get-Item -LiteralPath $stagedExecutable).VersionInfo
    if ($stagedVersion.FileVersion -ne $resourceVersion -or
        $stagedVersion.ProductVersion -ne $productVersion) {
        throw "The copied installer executable changed version metadata during staging."
    }

    $forbiddenArtifacts = Get-ChildItem -LiteralPath $staging -Recurse -Force -File |
        Where-Object {
            $_.Name -ieq "filters.ini" -or
            (Test-PdwPrivateTransactionArtifactName $_.Name)
        }
    if ($forbiddenArtifacts) {
        throw "Installer staging contains a retired filter or local-audio transaction artifact: $($forbiddenArtifacts.FullName -join ', ')"
    }

    $secretKeys = Get-ChildItem -LiteralPath $staging -Recurse -Force -File |
        Where-Object {
            $_.Name -match '(?i)\.ini(?:\.|$)' -or
            (Test-PdwPrivateTransactionArtifactName $_.Name)
        } |
        ForEach-Object {
            $iniPath = $_.FullName
            Get-Content -LiteralPath $iniPath | ForEach-Object {
                $sensitiveField = Get-PdwSensitiveIniFieldName $_
                if ($sensitiveField) {
                    "$iniPath`: $sensitiveField"
                }
            }
        }
    if ($secretKeys) {
        throw "An installer input INI or transaction artifact contains a non-empty sensitive field: $($secretKeys -join ', ')"
    }
    Assert-PdwExactCleanGitHead $sourceRoot $sourceCommit "finalizing installer-input staging"
    Write-PdwInstallerInputManifest $staging $Architecture $sourceCommit
    Assert-PdwInstallerInputManifest $staging $Architecture $sourceCommit
    $result = [pscustomobject]@{
        Architecture = $Architecture
        Directory = $destinationRoot
        Files = (Get-ChildItem -LiteralPath $staging -Recurse -Force -File).Count
        ExecutableSHA256 = (Get-FileHash -LiteralPath $stagedExecutable -Algorithm SHA256).Hash
    }
    # Keep the atomic no-replace rename as the final fallible operation so a
    # failed command cannot leave a release-named staged-input directory.
    Move-PdwDirectoryNoReplace $staging $destinationRoot
    $result
}
catch {
    if (Test-Path -LiteralPath $staging) {
        $resolved = (Resolve-Path -LiteralPath $staging).Path
        if ($resolved.StartsWith($parent + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
    throw
}
