[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Setup,
    [Parameter(Mandatory = $true)]
    [string]$TestRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$setupPath = (Resolve-Path -LiteralPath $Setup).Path
$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$preserveFixture = Join-Path $PSScriptRoot "fixtures\installer\PDW-upgrade-preserve.INI"
$root = [System.IO.Path]::GetFullPath($TestRoot)
if (Test-Path -LiteralPath $root) {
    throw "Installer smoke target already exists; refusing to overwrite: $root"
}
New-Item -ItemType Directory -Path $root | Out-Null
$script:setupInvocation = 0

function Get-PeMachine([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    return [BitConverter]::ToUInt16($bytes, $peOffset + 4)
}

function Invoke-Setup([string]$Architecture, [string]$InstallDirectory) {
    $script:setupInvocation++
    $log = Join-Path $root ("setup-{0}-{1}.log" -f $Architecture, $script:setupInvocation)
    $arguments = @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS",
        "/ARCH=$Architecture", ('/DIR="' + $InstallDirectory + '"'),
        ('/LOG="' + $log + '"')
    )
    $process = Start-Process -FilePath $setupPath -ArgumentList $arguments -Wait -PassThru
    $process.Refresh()
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $logSucceeded = (Test-Path -LiteralPath $log -PathType Leaf) -and
            [bool](Select-String -LiteralPath $log -SimpleMatch "Installation process succeeded.")
        if (-not $logSucceeded) { Start-Sleep -Milliseconds 100 }
    } while (-not $logSucceeded -and [DateTime]::UtcNow -lt $deadline)
    if ($process.ExitCode -ne 0 -and -not $logSucceeded) {
        throw "Setup failed for $Architecture with exit code $($process.ExitCode)."
    }
}

function Invoke-Uninstall([string]$InstallDirectory) {
    $uninstaller = Get-ChildItem -LiteralPath $InstallDirectory -Filter "unins*.exe" -File |
        Select-Object -First 1
    if (-not $uninstaller) { throw "Uninstaller was not created under $InstallDirectory." }
    $process = Start-Process -FilePath $uninstaller.FullName -ArgumentList @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART") -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Uninstall failed with exit code $($process.ExitCode)."
    }
}

function Test-Architecture([string]$Architecture, [uint16]$ExpectedMachine) {
    $installDirectory = Join-Path $root $Architecture
    Invoke-Setup $Architecture $installDirectory
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
    $executable = Join-Path $installDirectory "$displayName.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Installed executable is missing for $Architecture."
    }
    $machine = Get-PeMachine $executable
    if ($machine -ne $ExpectedMachine) {
        throw ("Installed {0} PE machine is 0x{1:X4}." -f $Architecture, $machine)
    }
    $version = (Get-Item -LiteralPath $executable).VersionInfo
    if ($version.FileVersion -ne $resourceVersion -or
        $version.ProductVersion -ne $productVersion) {
        throw "Installed executable metadata is incorrect for $Architecture."
    }
    foreach ($relative in @("PDW.INI", "filters.ini", "Receivers", "Wavfiles")) {
        if (-not (Test-Path -LiteralPath (Join-Path $installDirectory $relative))) {
            throw "Co-located installer content is missing for $Architecture $relative."
        }
    }
    if ($Architecture -eq "x64" -and
        (Test-Path -LiteralPath (Join-Path $installDirectory "Receivers\RTL-SDR\rtlsdr.dll"))) {
        throw "The x64 installation contains the x86-only RTL-SDR DLL."
    }
    if ($Architecture -eq "Win32" -and
        -not (Test-Path -LiteralPath (Join-Path $installDirectory "Receivers\RTL-SDR\rtlsdr.dll"))) {
        throw "The Win32 installation is missing the bundled x86 RTL-SDR DLL."
    }

    Copy-Item -LiteralPath $preserveFixture -Destination (Join-Path $installDirectory "PDW.INI") -Force
    $preserveHash = (Get-FileHash -LiteralPath (Join-Path $installDirectory "PDW.INI") -Algorithm SHA256).Hash
    $customReceiver = Join-Path $installDirectory "Receivers\custom-receiver.ini"
    Set-Content -LiteralPath $customReceiver -Value "[Receiver]`r`nName=Installer smoke custom receiver" -Encoding Ascii
    $customReceiverHash = (Get-FileHash -LiteralPath $customReceiver -Algorithm SHA256).Hash
    $predecessorExecutable = Join-Path $installDirectory "PDW v5 2026 Release.exe"
    Set-Content -LiteralPath $predecessorExecutable -Value "synthetic predecessor executable" -Encoding Ascii
    Invoke-Setup $Architecture $installDirectory
    if ((Get-FileHash -LiteralPath (Join-Path $installDirectory "PDW.INI") -Algorithm SHA256).Hash -ne
        $preserveHash) {
        throw "Upgrade overwrote the operator PDW.INI for $Architecture."
    }
    if ((Get-FileHash -LiteralPath $customReceiver -Algorithm SHA256).Hash -ne
        $customReceiverHash) {
        throw "Upgrade overwrote the operator receiver for $Architecture."
    }
    if (Test-Path -LiteralPath $predecessorExecutable -PathType Leaf) {
        throw "Upgrade left the renamed v5 predecessor executable for $Architecture."
    }

    Invoke-Uninstall $installDirectory
    if (Test-Path -LiteralPath $executable -PathType Leaf) {
        throw "Uninstall left the application executable for $Architecture."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $installDirectory "PDW.INI") -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $installDirectory "filters.ini") -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $installDirectory "Receivers\custom-receiver.ini") -PathType Leaf)) {
        throw "Uninstall removed preserved operator data for $Architecture."
    }
    [pscustomobject]@{
        Architecture = $Architecture
        Machine = ("0x{0:X4}" -f $machine)
        Version = $version.ProductVersion
        CoLocatedSettings = "Passed"
        UpgradePreservation = "Passed"
        UninstallPreservation = "Passed"
    }
}

Test-Architecture "x64" 0x8664
Test-Architecture "Win32" 0x014c
