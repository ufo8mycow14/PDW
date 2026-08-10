[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Win32ApplicationDirectory,
    [Parameter(Mandatory = $true)]
    [string]$X64ApplicationDirectory,
    [string]$OutputDirectory = "",
    [string]$InnoCompiler = "",
    [string]$SignToolName = "",
    [string]$SignToolCommand = "",
    [switch]$RequireSignature,
    [switch]$ScanWithDefender
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$win32Root = (Resolve-Path -LiteralPath $Win32ApplicationDirectory).Path
$x64Root = (Resolve-Path -LiteralPath $X64ApplicationDirectory).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $sourceRoot "out\installer"
}
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $outputRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $outputRoot | Out-Null
}

if ([string]::IsNullOrWhiteSpace($InnoCompiler)) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
    )
    $InnoCompiler = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($InnoCompiler) -or
    -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
    throw "Inno Setup Compiler 6 was not found. Install official Inno Setup or pass -InnoCompiler."
}

$setup = Join-Path $outputRoot "PDW-v5-2026-Release-Setup.exe"
if (Test-Path -LiteralPath $setup -PathType Leaf) {
    throw "Installer output already exists; refusing to overwrite: $setup"
}
if ([string]::IsNullOrWhiteSpace($SignToolName) -ne [string]::IsNullOrWhiteSpace($SignToolCommand)) {
    throw "SignToolName and SignToolCommand must be supplied together."
}

$arguments = @(
    "/DWin32Application=$win32Root",
    "/DX64Application=$x64Root",
    "/DInstallerOutput=$outputRoot"
)
if (-not [string]::IsNullOrWhiteSpace($SignToolName)) {
    $arguments += "/DSignToolName=$SignToolName"
    $arguments += "/S$SignToolName=$SignToolCommand"
}
$arguments += (Join-Path $sourceRoot "installer\PDW.iss")
& $InnoCompiler @arguments
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) {
    throw "Inno Setup did not produce the expected Setup executable."
}

$signature = Get-AuthenticodeSignature -LiteralPath $setup
if ($RequireSignature -and $signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
    throw "Setup does not have a valid trusted Authenticode signature: $($signature.Status)"
}
if ($ScanWithDefender) {
    $defender = Join-Path $env:ProgramFiles "Windows Defender\MpCmdRun.exe"
    if (-not (Test-Path -LiteralPath $defender -PathType Leaf)) {
        throw "Microsoft Defender command-line scanner was not found."
    }
    & $defender -Scan -ScanType 3 -File $setup -DisableRemediation
    if ($LASTEXITCODE -ne 0) { throw "Microsoft Defender did not return a clean result for Setup." }
}

$hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash
[System.IO.File]::WriteAllText("$setup.sha256", "$hash  $([System.IO.Path]::GetFileName($setup))`r`n",
    [System.Text.UTF8Encoding]::new($false))
[pscustomobject]@{
    Setup = $setup
    Size = (Get-Item -LiteralPath $setup).Length
    SHA256 = $hash
    Signature = $signature.Status
    DefenderScanned = [bool]$ScanWithDefender
}
