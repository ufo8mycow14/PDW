[CmdletBinding()]
param(
    [ValidateSet("Win32", "x64")]
    [string]$Architecture = "Win32",
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$BuildDirectory = "",
    [string]$OutputRoot = "",
    [string]$LegacyAssetsRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

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

if ([string]::IsNullOrWhiteSpace($LegacyAssetsRoot)) {
    $LegacyAssetsRoot = Split-Path -Parent $SourceRoot
}
$LegacyAssetsRoot = Resolve-RequiredDirectory $LegacyAssetsRoot "Legacy assets"

$versionHeader = Get-Content -LiteralPath (Join-Path $SourceRoot "Headers\version.h") -Raw
$major = [regex]::Match($versionHeader, '#define PDW_VERSION_MAJOR ([0-9]+)').Groups[1].Value
$minor = [regex]::Match($versionHeader, '#define PDW_VERSION_MINOR ([0-9]+)').Groups[1].Value
$patch = [regex]::Match($versionHeader, '#define PDW_VERSION_PATCH ([0-9]+)').Groups[1].Value
if (-not $major -or -not $minor -or -not $patch) {
    throw "Unable to read the canonical version from Headers\version.h."
}

$version = "$major.$minor.$patch"
function Read-StringMacro([string]$Name) {
    $match = [regex]::Match($versionHeader,
        '(?m)^#define ' + [regex]::Escape($Name) + ' "([^"]+)"$')
    if (-not $match.Success) { throw "Unable to read $Name from Headers\version.h." }
    return $match.Groups[1].Value
}
$displayName = Read-StringMacro "PDW_DISPLAY_VERSION"
$productVersion = Read-StringMacro "PDW_VERSION_STRING"
$packageBase = Read-StringMacro "PDW_PACKAGE_BASENAME"
$folderName = "$packageBase-$Architecture"
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

$versionInfo = (Get-Item -LiteralPath $executable).VersionInfo
if ($versionInfo.FileVersion -ne "$version.0" -or
    $versionInfo.ProductVersion -ne $productVersion) {
    throw "Executable metadata does not match $displayName."
}

$gitStatus = & git -C $SourceRoot status --porcelain --untracked-files=all
if ($LASTEXITCODE -ne 0) { throw "Unable to inspect Git status." }
if ($gitStatus) { throw "The source tree must be clean before packaging.`n$gitStatus" }

$finalDirectory = Join-Path $OutputRoot $folderName
$finalZip = Join-Path $OutputRoot "$folderName.zip"
if ((Test-Path -LiteralPath $finalDirectory) -or (Test-Path -LiteralPath $finalZip)) {
    throw "Release output already exists; refusing to overwrite $folderName."
}

$temporaryRoot = Join-Path $OutputRoot "tmp"
if (-not (Test-Path -LiteralPath $temporaryRoot)) {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
}
$temporaryRoot = (Resolve-Path -LiteralPath $temporaryRoot).Path
$staging = Join-Path $temporaryRoot ("pdw-package-" + [guid]::NewGuid().ToString("N"))
$application = Join-Path $staging "Application"

try {
    New-Item -ItemType Directory -Path $application | Out-Null

    $applicationFiles = @(
        @{ Source = $executable; Name = "$displayName.exe" },
        @{ Source = (Join-Path $SourceRoot "packaging\PDW.INI"); Name = "PDW.INI" },
        @{ Source = (Join-Path $SourceRoot "packaging\filters.ini"); Name = "filters.ini" },
        @{ Source = (Join-Path $SourceRoot "pdw-manual.pdf"); Name = "PDW.pdf" },
        @{ Source = (Join-Path $SourceRoot "Readme"); Name = "Readme" },
        @{ Source = (Join-Path $SourceRoot "CHANGELOG.md"); Name = "CHANGELOG.md" },
        @{ Source = (Join-Path $SourceRoot "License"); Name = "License" },
        @{ Source = (Join-Path $SourceRoot "THIRD_PARTY_NOTICES.md"); Name = "THIRD_PARTY_NOTICES.md" }
    )
    foreach ($entry in $applicationFiles) {
        if (-not (Test-Path -LiteralPath $entry.Source -PathType Leaf)) {
            throw "Required package file is missing: $($entry.Source)"
        }
        Copy-Item -LiteralPath $entry.Source -Destination (Join-Path $application $entry.Name)
    }

    Copy-Item -LiteralPath (Join-Path $SourceRoot "docs") -Destination $application -Recurse
    Copy-Item -LiteralPath (Join-Path $SourceRoot "Receivers") -Destination $application -Recurse
    if ($Architecture -eq "x64") {
        # The bundled RTL-SDR DLL is intentionally x86-only. The x64 build can
        # use RTL-TCP immediately or import a trusted matching x64 receiver DLL.
        $bundledX86Receiver = Join-Path $application "Receivers\RTL-SDR\rtlsdr.dll"
        if (Test-Path -LiteralPath $bundledX86Receiver -PathType Leaf) {
            Remove-Item -LiteralPath $bundledX86Receiver
        }
    }

    $wavSource = Join-Path $SourceRoot "packaging\Wavfiles"
    $legacyDataSource = Join-Path $SourceRoot "packaging\Legacy"
    Copy-Item -LiteralPath $wavSource -Destination $application -Recurse

    $legacyNames = @("base-ids.txt", "language.df", "PDW.HLP")
    foreach ($legacyName in $legacyNames) {
        $legacyPath = Join-Path $legacyDataSource $legacyName
        Copy-Item -LiteralPath $legacyPath -Destination (Join-Path $application $legacyName)
    }
    if ($Architecture -eq "Win32") {
        foreach ($legacyDriver in @("COMPRT.VXD", "Comprt2.vxd", "xp_driver.zip")) {
            $legacyPath = Join-Path $LegacyAssetsRoot $legacyDriver
            if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
                Copy-Item -LiteralPath $legacyPath -Destination (Join-Path $application $legacyDriver)
            }
        }
    }

    # Mirror the ready-to-run application at the package root while retaining
    # an explicit Application folder for users who prefer that layout.
    Get-ChildItem -LiteralPath $application -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $staging -Recurse
    }

    $sourceArchive = Join-Path $staging "source.zip"
    & git -C $SourceRoot archive --format=zip --output=$sourceArchive HEAD
    if ($LASTEXITCODE -ne 0) { throw "git archive failed." }
    Expand-Archive -LiteralPath $sourceArchive -DestinationPath (Join-Path $staging "Source")
    Remove-Item -LiteralPath $sourceArchive

    $forbidden = Get-ChildItem -LiteralPath $staging -Recurse -Force -File | Where-Object {
        $_.Name -match '(?i)\.(log|sqlite|sqlite3|db|iq|sigmf-data)$' -or
        $_.FullName -match '(?i)[\\/](Published|PublishQueue|DeadLetter)[\\/]'
    }
    if ($forbidden) {
        throw "Generated/private runtime files were found in the package: $($forbidden.FullName -join ', ')"
    }

    $nonEmptySecret = Get-ChildItem -LiteralPath $staging -Recurse -Force -File -Filter *.ini |
        ForEach-Object {
            $iniPath = $_.FullName
            Get-Content -LiteralPath $iniPath | ForEach-Object {
                if ($_ -match '(?i)^\s*(Password|Token|Secret|ApiKey|BearerToken)\s*=\s*\S+') {
                    "$iniPath`: $($Matches[1])"
                }
            }
        }
    if ($nonEmptySecret) {
        throw "A staged INI contains a non-empty secret field: $($nonEmptySecret -join ', ')"
    }

    $hashLines = Get-ChildItem -LiteralPath $staging -Recurse -Force -File |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring($staging.Length + 1).Replace('\', '/')
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $relative"
        }
    [System.IO.File]::WriteAllLines((Join-Path $staging "SHA256SUMS.txt"), $hashLines,
        [System.Text.UTF8Encoding]::new($false))

    Move-Item -LiteralPath $staging -Destination $finalDirectory
    Compress-Archive -LiteralPath $finalDirectory -DestinationPath $finalZip -CompressionLevel Optimal

    $packagedExecutable = Join-Path $finalDirectory "$displayName.exe"
    $packagedHash = (Get-FileHash -LiteralPath $packagedExecutable -Algorithm SHA256).Hash
    [pscustomobject]@{
        Version = $version
        Architecture = $Architecture
        Folder = $finalDirectory
        Zip = $finalZip
        Files = (Get-ChildItem -LiteralPath $finalDirectory -Recurse -Force -File).Count
        ExecutableSHA256 = $packagedHash
        ZipSHA256 = (Get-FileHash -LiteralPath $finalZip -Algorithm SHA256).Hash
    }
}
catch {
    if (Test-Path -LiteralPath $staging) {
        $resolvedStaging = (Resolve-Path -LiteralPath $staging).Path
        if ($resolvedStaging.StartsWith($temporaryRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
        }
    }
    throw
}
