[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Setup,
    [switch]$RequireSignature,
    [switch]$ScanWithDefender
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$versionHeader = Get-Content -LiteralPath (Join-Path $sourceRoot "Headers\version.h") -Raw
function Read-StringMacro([string]$Name) {
    $match = [regex]::Match($versionHeader,
        '(?m)^#define ' + [regex]::Escape($Name) + ' "([^"]+)"\r?$')
    if (-not $match.Success) { throw "Unable to read $Name from Headers\version.h." }
    return $match.Groups[1].Value
}
$displayName = Read-StringMacro "PDW_DISPLAY_VERSION"
$resourceVersion = Read-StringMacro "PDW_VERSION_RESOURCE_STRING"
$packageBase = Read-StringMacro "PDW_PACKAGE_BASENAME"
$setupPath = (Resolve-Path -LiteralPath $Setup).Path
if ([System.IO.Path]::GetFileName($setupPath) -ne "$packageBase-Setup.exe") {
    throw "Unexpected installer filename: $setupPath"
}
$version = (Get-Item -LiteralPath $setupPath).VersionInfo
if ($version.FileVersion.Trim() -ne $resourceVersion -or
    $version.ProductVersion.Trim() -ne $resourceVersion -or
    $version.ProductName.Trim() -ne $displayName -or
    $version.FileDescription.Trim() -ne "$displayName Setup") {
    throw "Setup version metadata does not match $displayName."
}
$bytes = [System.IO.File]::ReadAllBytes($setupPath)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
if ($machine -ne 0x014c) {
    throw ("Setup bootstrapper PE machine is 0x{0:X4}, expected x86-compatible 0x014C." -f $machine)
}
$signature = Get-AuthenticodeSignature -LiteralPath $setupPath
if ($RequireSignature -and $signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
    throw "Public release requires a valid trusted Authenticode signature; found $($signature.Status)."
}
if ($ScanWithDefender) {
    $defender = Join-Path $env:ProgramFiles "Windows Defender\MpCmdRun.exe"
    if (-not (Test-Path -LiteralPath $defender -PathType Leaf)) {
        throw "Microsoft Defender command-line scanner was not found."
    }
    & $defender -Scan -ScanType 3 -File $setupPath -DisableRemediation
    if ($LASTEXITCODE -ne 0) { throw "Microsoft Defender did not return a clean result for Setup." }
}
[pscustomobject]@{
    Setup = $setupPath
    Machine = "0x014C"
    Version = $version.ProductName.Trim()
    SHA256 = (Get-FileHash -LiteralPath $setupPath -Algorithm SHA256).Hash
    Signature = $signature.Status
    PublicReleaseReady = $signature.Status -eq [System.Management.Automation.SignatureStatus]::Valid
    DefenderScanned = [bool]$ScanWithDefender
}
