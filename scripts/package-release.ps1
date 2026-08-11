[CmdletBinding()]
param(
    [ValidateSet("Win32", "x64")]
    [string]$Architecture = "Win32",
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$BuildDirectory = "",
    [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "release-provenance.ps1")

function Resolve-RequiredDirectory([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label directory does not exist: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Get-PeMachine([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        if ($stream.Length -lt 64) { throw "Executable is too small to be a Windows PE file: $Path" }
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Executable has no DOS header: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset + 6 -gt $stream.Length) {
            throw "Executable has an invalid PE offset: $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Executable has no PE signature: $Path" }
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

$SourceRoot = Resolve-RequiredDirectory $SourceRoot "Source"
& (Join-Path $SourceRoot "scripts\audit-release.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "Repository release audit failed."
}
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $buildName = if ($Architecture -eq "x64") { "build-x64" } else { "build-win32" }
    $BuildDirectory = Join-Path $SourceRoot "out\$buildName\Release"
}
$BuildDirectory = Resolve-RequiredDirectory $BuildDirectory "Build"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Split-Path -Parent $SourceRoot
}
$OutputRoot = Resolve-RequiredDirectory $OutputRoot "Output root"

$sourceCommit = Get-PdwExactCleanGitHead $SourceRoot "portable packaging"
$sourceCommitTimeText = (& git -C $SourceRoot show -s --format=%cI $sourceCommit).Trim()
if ($LASTEXITCODE -ne 0) { throw "Unable to resolve the source commit time." }
$sourceCommitTime = [DateTimeOffset]::Parse($sourceCommitTimeText).UtcDateTime

$temporaryRoot = Join-Path $OutputRoot "tmp"
if (-not (Test-Path -LiteralPath $temporaryRoot)) {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
}
$temporaryRoot = (Resolve-Path -LiteralPath $temporaryRoot).Path
$staging = Join-Path $temporaryRoot ("pdw-package-" + [guid]::NewGuid().ToString("N"))
$candidateRoot = $null
$validationRoot = $null
$temporaryZip = $null
$publicationSet = $null
$finalSet = $null
$finalDirectory = $null
$finalZip = $null
$publishedSet = $false

try {
    New-Item -ItemType Directory -Path $staging | Out-Null
    $sourceSnapshot = Join-Path $staging ".source-snapshot"
    New-PdwGitCommitSnapshot $SourceRoot $sourceCommit $sourceSnapshot

    $standardProfile = Join-Path $sourceSnapshot "packaging\PDW.INI"
    $adelaideProfile = Join-Path $sourceSnapshot "packaging\PDW-Adelaide-FLEX.INI"
    foreach ($profilePath in @($standardProfile, $adelaideProfile)) {
        if (-not (Test-Path -LiteralPath $profilePath -PathType Leaf)) {
            throw "Required clean-install profile is missing from commit $sourceCommit`: $profilePath"
        }
    }
    $adelaideProfileText = Get-Content -LiteralPath $adelaideProfile -Raw
    foreach ($requiredSetting in @(
        'PresetId=sdrsharp-vbcable-adelaide-flex-v1',
        'DeviceFriendlyName=CABLE Output (VB-Audio Virtual Cable)',
        'AudioConfiguration=0',
        'BTSYNC=13107',
        'Threshold1600=2'
    )) {
        if ($adelaideProfileText -notmatch ('(?m)^' + [regex]::Escape($requiredSetting) + '\r?$')) {
            throw "The Adelaide FLEX package profile is incomplete: $requiredSetting"
        }
    }

    $versionHeader = Get-Content -LiteralPath (Join-Path $sourceSnapshot "Headers\version.h") -Raw
    $major = [regex]::Match($versionHeader, '#define PDW_VERSION_MAJOR ([0-9]+)').Groups[1].Value
    $minor = [regex]::Match($versionHeader, '#define PDW_VERSION_MINOR ([0-9]+)').Groups[1].Value
    $patch = [regex]::Match($versionHeader, '#define PDW_VERSION_PATCH ([0-9]+)').Groups[1].Value
    if (-not $major -or -not $minor -or -not $patch) {
        throw "Unable to read the canonical version from Headers\version.h."
    }
    $version = "$major.$minor.$patch"
    function Read-StringMacro([string]$Name) {
        $match = [regex]::Match($versionHeader,
            '(?m)^#define ' + [regex]::Escape($Name) + ' "([^"]+)"\r?$')
        if (-not $match.Success) { throw "Unable to read $Name from Headers\version.h." }
        return $match.Groups[1].Value
    }
    $displayName = Read-StringMacro "PDW_DISPLAY_VERSION"
    $productVersion = Read-StringMacro "PDW_VERSION_STRING"
    $packageBase = Read-StringMacro "PDW_PACKAGE_BASENAME"
    $folderName = "$packageBase-$Architecture"
    $setName = "$folderName-package"
    $finalSet = Join-Path $OutputRoot $setName
    $finalDirectory = Join-Path $finalSet $folderName
    $finalZip = Join-Path $finalSet "$folderName.zip"
    if ((Test-Path -LiteralPath $finalSet) -or
        (Test-Path -LiteralPath (Join-Path $OutputRoot $folderName)) -or
        (Test-Path -LiteralPath (Join-Path $OutputRoot "$folderName.zip"))) {
        throw "Release output already exists; refusing to overwrite $folderName."
    }

    $executable = Join-Path $BuildDirectory "$displayName.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Release executable does not exist: $executable"
    }
    $expectedMachine = if ($Architecture -eq "x64") { 0x8664 } else { 0x014C }
    $actualMachine = Get-PeMachine $executable
    if ($actualMachine -ne $expectedMachine) {
        throw ("Executable architecture does not match {0}: PE machine 0x{1:X4}." -f
            $Architecture, $actualMachine)
    }
    $runtimeFiles = @(Assert-AppLocalRuntimeSet $BuildDirectory $Architecture $expectedMachine)
    $buildCommitMarker = Assert-BuildCommitMarker $BuildDirectory $sourceCommit
    $versionInfo = (Get-Item -LiteralPath $executable).VersionInfo
    if ($versionInfo.FileVersion -ne "$version.0" -or
        $versionInfo.ProductVersion -ne $productVersion) {
        throw "Executable metadata does not match $displayName."
    }
    if ((Get-Item -LiteralPath $executable).LastWriteTimeUtc -lt $sourceCommitTime) {
        throw "The release executable predates source commit $sourceCommit; rebuild this exact clean commit before packaging."
    }

    $application = Join-Path $staging "Application"
    New-Item -ItemType Directory -Path $application | Out-Null
    $profilesDirectory = Join-Path $application "Profiles"
    New-Item -ItemType Directory -Path $profilesDirectory | Out-Null

    Copy-PdwFileHashStable $executable (Join-Path $application "$displayName.exe")
    Copy-PdwFileHashStable $buildCommitMarker (Join-Path $application "PDW_BUILD_COMMIT.txt")
    $applicationFiles = @(
        @{ Source = $standardProfile; Name = "PDW.INI" },
        @{ Source = (Join-Path $sourceSnapshot "pdw-manual.pdf"); Name = "PDW.pdf" },
        @{ Source = (Join-Path $sourceSnapshot "Readme"); Name = "Readme" },
        @{ Source = (Join-Path $sourceSnapshot "CHANGELOG.md"); Name = "CHANGELOG.md" },
        @{ Source = (Join-Path $sourceSnapshot "License"); Name = "License" },
        @{ Source = (Join-Path $sourceSnapshot "THIRD_PARTY_NOTICES.md"); Name = "THIRD_PARTY_NOTICES.md" }
    )
    foreach ($entry in $applicationFiles) {
        if (-not (Test-Path -LiteralPath $entry.Source -PathType Leaf)) {
            throw "Required package file is missing: $($entry.Source)"
        }
        Copy-Item -LiteralPath $entry.Source -Destination (Join-Path $application $entry.Name)
    }
    foreach ($runtime in $runtimeFiles) {
        Copy-PdwFileHashStable $runtime.FullName (Join-Path $application $runtime.Name)
    }
    Copy-Item -LiteralPath $adelaideProfile -Destination (Join-Path $profilesDirectory "PDW-Adelaide-FLEX.INI")

    Copy-Item -LiteralPath (Join-Path $sourceSnapshot "docs") -Destination $application -Recurse
    Copy-Item -LiteralPath (Join-Path $sourceSnapshot "Receivers") -Destination $application -Recurse
    if ($Architecture -eq "x64") {
        # The bundled RTL-SDR DLL is intentionally x86-only. The x64 build can
        # use RTL-TCP immediately or import a trusted matching x64 receiver DLL.
        $bundledX86Receiver = Join-Path $application "Receivers\RTL-SDR\rtlsdr.dll"
        if (Test-Path -LiteralPath $bundledX86Receiver -PathType Leaf) {
            Remove-Item -LiteralPath $bundledX86Receiver
        }
    }

    $wavSource = Join-Path $sourceSnapshot "packaging\Wavfiles"
    $legacyDataSource = Join-Path $sourceSnapshot "packaging\Legacy"
    Copy-Item -LiteralPath $wavSource -Destination $application -Recurse

    $legacyNames = @("base-ids.txt", "language.df", "PDW.HLP")
    foreach ($legacyName in $legacyNames) {
        $legacyPath = Join-Path $legacyDataSource $legacyName
        Copy-Item -LiteralPath $legacyPath -Destination (Join-Path $application $legacyName)
    }

    $copiedExecutable = Join-Path $application "$displayName.exe"
    if ((Get-PeMachine $copiedExecutable) -ne $expectedMachine) {
        throw "The copied package executable changed architecture during staging."
    }
    [void](Assert-AppLocalRuntimeSet $application $Architecture $expectedMachine)
    [void](Assert-BuildCommitMarker $application $sourceCommit)
    $copiedVersionInfo = (Get-Item -LiteralPath $copiedExecutable).VersionInfo
    if ($copiedVersionInfo.FileVersion -ne "$version.0" -or
        $copiedVersionInfo.ProductVersion -ne $productVersion) {
        throw "The copied package executable changed version metadata during staging."
    }

    # Mirror the ready-to-run application at the package root while retaining
    # an explicit Application folder for users who prefer that layout.
    Get-ChildItem -LiteralPath $application -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $staging -Recurse
    }

    $sourceDirectory = Join-Path $staging "Source"
    Copy-Item -LiteralPath $sourceSnapshot -Destination $sourceDirectory -Recurse
    Remove-Item -LiteralPath $sourceSnapshot -Recurse -Force
    [System.IO.File]::WriteAllText((Join-Path $staging "SOURCE_COMMIT.txt"),
        "$sourceCommit`r`n", [System.Text.UTF8Encoding]::new($false))
    $sourceProvenance = Join-Path $sourceDirectory "PDW_SOURCE_PROVENANCE.txt"
    [System.IO.File]::WriteAllText($sourceProvenance,
        "commit=$sourceCommit`nstate=clean`norigin=git-archive`n",
        [System.Text.UTF8Encoding]::new($false))
    $sourceHashManifest = Join-Path $sourceDirectory "PDW_SOURCE_SHA256SUMS.txt"
    $sourceHashLines = Get-ChildItem -LiteralPath $sourceDirectory -Recurse -Force -File |
        Where-Object { $_.FullName -ne $sourceHashManifest } |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring($sourceDirectory.Length + 1).Replace('\', '/')
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $relative"
        }
    [System.IO.File]::WriteAllLines($sourceHashManifest, $sourceHashLines,
        [System.Text.UTF8Encoding]::new($false))

    $forbidden = Get-ChildItem -LiteralPath $staging -Recurse -Force -File | Where-Object {
        $_.Name -ieq 'filters.ini' -or
        (Test-PdwPrivateTransactionArtifactName $_.Name) -or
        $_.Name -match '(?i)\.(log|sqlite|sqlite3|db|iq|sigmf-data)$' -or
        $_.FullName -match '(?i)[\\/](Published|PublishQueue|DeadLetter)[\\/]'
    }
    if ($forbidden) {
        throw "Generated/private runtime files were found in the package: $($forbidden.FullName -join ', ')"
    }

    $nonEmptySecret = Get-ChildItem -LiteralPath $staging -Recurse -Force -File |
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
    if ($nonEmptySecret) {
        throw "A staged INI or transaction artifact contains a non-empty sensitive field: $($nonEmptySecret -join ', ')"
    }

    Assert-PdwExactCleanGitHead $SourceRoot $sourceCommit "finalizing portable packaging"

    Write-PdwDirectoryHashManifest $staging
    Assert-PdwDirectoryHashManifest $staging

    $candidateRoot = Join-Path $temporaryRoot ("pdw-package-candidate-" +
        [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $candidateRoot | Out-Null
    $candidateDirectory = Join-Path $candidateRoot $folderName
    Move-Item -LiteralPath $staging -Destination $candidateDirectory
    $staging = $candidateDirectory
    Assert-PdwDirectoryHashManifest $staging

    $temporaryZip = Join-Path $candidateRoot "$folderName-candidate.zip"
    Compress-Archive -LiteralPath $staging -DestinationPath $temporaryZip `
        -CompressionLevel Optimal
    $validationRoot = Join-Path $candidateRoot "expanded-validation"
    Expand-Archive -LiteralPath $temporaryZip -DestinationPath $validationRoot
    $expandedDirectory = Join-Path $validationRoot $folderName
    Assert-PdwDirectoryHashManifest $expandedDirectory
    Assert-PdwExactCleanGitHead $SourceRoot $sourceCommit "validating portable output"

    Assert-PdwDirectoryHashManifest $staging
    $packagedExecutable = Join-Path $staging "$displayName.exe"
    $packagedHash = (Get-FileHash -LiteralPath $packagedExecutable -Algorithm SHA256).Hash
    $temporaryZipHash = (Get-FileHash -LiteralPath $temporaryZip -Algorithm SHA256).Hash

    # Publish the expanded folder and ZIP as one atomic no-replace directory
    # rename. This prevents either half of the architecture package from being
    # left under a release name when the other destination becomes occupied.
    $publicationSet = Join-Path $candidateRoot $setName
    New-Item -ItemType Directory -Path $publicationSet | Out-Null
    $setDirectory = Join-Path $publicationSet $folderName
    $setZip = Join-Path $publicationSet "$folderName.zip"
    Move-PdwDirectoryNoReplace $staging $setDirectory
    Move-PdwFileNoReplace $temporaryZip $setZip
    Assert-PdwDirectoryHashManifest $setDirectory
    $setZipHash = (Get-FileHash -LiteralPath $setZip -Algorithm SHA256).Hash
    if ($setZipHash -cne $temporaryZipHash) {
        throw "Portable ZIP changed while the atomic publication set was assembled."
    }
    Assert-PdwExactCleanGitHead $SourceRoot $sourceCommit "publishing portable output"

    $result = [pscustomobject]@{
        Version = $version
        Architecture = $Architecture
        Folder = $finalDirectory
        Zip = $finalZip
        Files = (Get-ChildItem -LiteralPath $setDirectory -Recurse -Force -File).Count
        ExecutableSHA256 = $packagedHash
        ZipSHA256 = $setZipHash
    }

    # No fallible validation follows this publication point. Directory.Move is
    # a same-volume, atomic, no-replace rename of the complete package set.
    Move-PdwDirectoryNoReplace $publicationSet $finalSet
    $publishedSet = $true
    if (Test-Path -LiteralPath $candidateRoot) {
        try {
            Remove-Item -LiteralPath $candidateRoot -Recurse -Force
        }
        catch {
            Write-Warning "Published portable package, but could not remove its empty private work root: $candidateRoot"
        }
    }
    $result
}
catch {
    if ($publishedSet) {
        # The publication point is deliberately the final fallible operation.
        # Keep this guard as a fail-safe if later maintenance adds new code.
        Write-Warning "A complete portable publication set was retained for explicit inspection: $finalSet"
    }
    if (Test-Path -LiteralPath $staging) {
        $resolvedStaging = (Resolve-Path -LiteralPath $staging).Path
        if ($resolvedStaging.StartsWith($temporaryRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
        }
    }
    if ($candidateRoot -and (Test-Path -LiteralPath $candidateRoot)) {
        $resolvedCandidateRoot = (Resolve-Path -LiteralPath $candidateRoot).Path
        if ($resolvedCandidateRoot.StartsWith(
            $temporaryRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolvedCandidateRoot -Recurse -Force
        }
    }
    throw
}
