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

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = [System.IO.Path]::GetFullPath($BuildDirectory)
$destinationRoot = [System.IO.Path]::GetFullPath($Destination)
if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
    throw "Build directory does not exist: $buildRoot"
}
if (Test-Path -LiteralPath $destinationRoot) {
    throw "Installer input destination already exists: $destinationRoot"
}

$versionHeader = Get-Content -LiteralPath (Join-Path $sourceRoot "Headers\version.h") -Raw
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

$bytes = [System.IO.File]::ReadAllBytes($executable)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
$expectedMachine = if ($Architecture -eq "x64") { 0x8664 } else { 0x014c }
if ($machine -ne $expectedMachine) {
    throw ("Executable does not match {0}: PE machine 0x{1:X4}." -f $Architecture, $machine)
}
$version = (Get-Item -LiteralPath $executable).VersionInfo
if ($version.FileVersion -ne $resourceVersion -or $version.ProductVersion -ne $productVersion) {
    throw "Executable version metadata does not match $displayName."
}

$parent = Split-Path -Parent $destinationRoot
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent | Out-Null
}
$staging = Join-Path $parent ("pdw-installer-input-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path $staging | Out-Null
    foreach ($file in @(
        @{ Source = $executable; Name = "$displayName.exe" },
        @{ Source = (Join-Path $sourceRoot "packaging\PDW.INI"); Name = "PDW.INI" },
        @{ Source = (Join-Path $sourceRoot "packaging\Legacy\base-ids.txt"); Name = "base-ids.txt" },
        @{ Source = (Join-Path $sourceRoot "packaging\Legacy\language.df"); Name = "language.df" },
        @{ Source = (Join-Path $sourceRoot "packaging\Legacy\PDW.HLP"); Name = "PDW.HLP" },
        @{ Source = (Join-Path $sourceRoot "pdw-manual.pdf"); Name = "PDW.pdf" },
        @{ Source = (Join-Path $sourceRoot "Readme"); Name = "Readme" },
        @{ Source = (Join-Path $sourceRoot "CHANGELOG.md"); Name = "CHANGELOG.md" },
        @{ Source = (Join-Path $sourceRoot "License"); Name = "License" },
        @{ Source = (Join-Path $sourceRoot "THIRD_PARTY_NOTICES.md"); Name = "THIRD_PARTY_NOTICES.md" }
    )) {
        if (-not (Test-Path -LiteralPath $file.Source -PathType Leaf)) {
            throw "Required installer file is missing: $($file.Source)"
        }
        Copy-Item -LiteralPath $file.Source -Destination (Join-Path $staging $file.Name)
    }
    Copy-Item -LiteralPath (Join-Path $sourceRoot "docs") -Destination $staging -Recurse
    Copy-Item -LiteralPath (Join-Path $sourceRoot "Receivers") -Destination $staging -Recurse
    Copy-Item -LiteralPath (Join-Path $sourceRoot "packaging\Wavfiles") -Destination $staging -Recurse
    if ($Architecture -eq "x64") {
        $x86Receiver = Join-Path $staging "Receivers\RTL-SDR\rtlsdr.dll"
        if (Test-Path -LiteralPath $x86Receiver -PathType Leaf) {
            Remove-Item -LiteralPath $x86Receiver
        }
    }

    $secretKeys = Get-ChildItem -LiteralPath $staging -Recurse -Force -File -Filter *.ini |
        ForEach-Object {
            $iniPath = $_.FullName
            Get-Content -LiteralPath $iniPath | ForEach-Object {
                if ($_ -match '(?i)^\s*(Password|Token|Secret|ApiKey|BearerToken)\s*=\s*\S+') {
                    "$iniPath`: $($Matches[1])"
                }
            }
        }
    if ($secretKeys) {
        throw "An installer input INI contains a non-empty secret field: $($secretKeys -join ', ')"
    }
    Move-Item -LiteralPath $staging -Destination $destinationRoot
    [pscustomobject]@{
        Architecture = $Architecture
        Directory = $destinationRoot
        Files = (Get-ChildItem -LiteralPath $destinationRoot -Recurse -Force -File).Count
        ExecutableSHA256 = (Get-FileHash -LiteralPath (Join-Path $destinationRoot "$displayName.exe") -Algorithm SHA256).Hash
    }
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
