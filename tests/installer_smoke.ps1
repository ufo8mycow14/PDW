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
$standardProfile = Join-Path $sourceRoot "packaging\PDW.INI"
$adelaideProfile = Join-Path $sourceRoot "packaging\PDW-Adelaide-FLEX.INI"
foreach ($required in @($preserveFixture, $standardProfile, $adelaideProfile)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required installer smoke input is missing: $required"
    }
}

$root = [System.IO.Path]::GetFullPath($TestRoot)
if (Test-Path -LiteralPath $root) {
    throw "Installer smoke target already exists; refusing to overwrite: $root"
}
New-Item -ItemType Directory -Path $root | Out-Null
$script:setupInvocation = 0

$versionHeader = Get-Content -LiteralPath (Join-Path $sourceRoot "Headers\version.h") -Raw
function Read-StringMacro([string]$Name) {
    $match = [regex]::Match($versionHeader,
        '(?m)^#define ' + [regex]::Escape($Name) + ' "([^"]+)"\r?$')
    if (-not $match.Success) { throw "Unable to read $Name from Headers/version.h." }
    return $match.Groups[1].Value
}

$displayName = Read-StringMacro "PDW_DISPLAY_VERSION"
$productVersion = Read-StringMacro "PDW_VERSION_STRING"
$resourceVersion = Read-StringMacro "PDW_VERSION_RESOURCE_STRING"
$standardProfileHash = (Get-FileHash -LiteralPath $standardProfile -Algorithm SHA256).Hash
$adelaideProfileHash = (Get-FileHash -LiteralPath $adelaideProfile -Algorithm SHA256).Hash
$sourceCommitText = & git -C $sourceRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) {
    throw "Unable to resolve the source commit for installer provenance smoke."
}
$sourceCommit = ([string]$sourceCommitText).Trim().ToLowerInvariant()
if ($sourceCommit -notmatch '^[0-9a-f]{40}$') {
    throw "Unable to resolve the source commit for installer provenance smoke."
}
$gitStatus = & git -C $sourceRoot status --porcelain --untracked-files=all
if ($LASTEXITCODE -ne 0 -or $gitStatus) {
    throw "Installer provenance smoke requires the exact clean source commit."
}
$expectedBuildMarker = "commit=$sourceCommit`nstate=clean`n"

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

function Get-RequiredAppLocalRuntimeNames([string]$Architecture) {
    $names = @($appLocalRuntimeAllowlist | Where-Object { $_ -ne "vcruntime140_1.dll" })
    if ($Architecture -eq "x64") { $names += "vcruntime140_1.dll" }
    return $names
}

function Get-PeMachine([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    return [BitConverter]::ToUInt16($bytes, $peOffset + 4)
}

function Assert-FileHash([string]$Path, [string]$ExpectedHash, [string]$Message) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Message File is missing: $Path"
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actualHash -ne $ExpectedHash) {
        throw "$Message Expected $ExpectedHash but found $actualHash."
    }
}

function Assert-AppOwnedFilesRemoved([string]$InstallDirectory, [string]$Architecture) {
    foreach ($name in @("$displayName.exe", "PDW_BUILD_COMMIT.txt") + $appLocalRuntimeAllowlist) {
        if (Test-Path -LiteralPath (Join-Path $InstallDirectory $name) -PathType Leaf) {
            throw "Uninstall left app-owned $Architecture file $name."
        }
    }
}

function Invoke-Setup {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("Win32", "x64")]
        [string]$Architecture,
        [Parameter(Mandatory = $true)]
        [string]$InstallDirectory,
        [ValidateSet("standard", "adelaide-flex")]
        [string]$Profile = "",
        [switch]$UseInstalledArchitecture
    )

    $script:setupInvocation++
    $profileLabel = if ($Profile) { $Profile } elseif ($UseInstalledArchitecture) {
        "preserve-architecture"
    } else { "default" }
    $log = Join-Path $root ("setup-{0}-{1}-{2}.log" -f
        $Architecture, $profileLabel, $script:setupInvocation)
    $arguments = @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS")
    if (-not $UseInstalledArchitecture) { $arguments += "/ARCH=$Architecture" }
    $arguments += ('/DIR="' + $InstallDirectory + '"')
    $arguments += ('/LOG="' + $log + '"')
    if ($Profile) { $arguments += "/PROFILE=$Profile" }
    $process = Start-Process -FilePath $setupPath -ArgumentList $arguments -Wait -PassThru
    $process.Refresh()
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $logSucceeded = (Test-Path -LiteralPath $log -PathType Leaf) -and
            [bool](Select-String -LiteralPath $log -SimpleMatch "Installation process succeeded.")
        if (-not $logSucceeded) { Start-Sleep -Milliseconds 100 }
    } while (-not $logSucceeded -and [DateTime]::UtcNow -lt $deadline)
    if ($process.ExitCode -ne 0 -or -not $logSucceeded) {
        throw "Setup failed for $Architecture/$profileLabel with exit code $($process.ExitCode)."
    }
}

function Invoke-ForcedReceiverPreservationFailure {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("Win32", "x64")]
        [string]$Architecture,
        [Parameter(Mandatory = $true)]
        [string]$InstallDirectory
    )

    $script:setupInvocation++
    $log = Join-Path $root ("setup-{0}-forced-receiver-rollback-{1}.log" -f
        $Architecture, $script:setupInvocation)
    $arguments = @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS",
        "/ARCH=$Architecture", "/TESTFAILAFTERRECEIVERPRESERVATION=1",
        ('/DIR="' + $InstallDirectory + '"'), ('/LOG="' + $log + '"')
    )
    $process = Start-Process -FilePath $setupPath -ArgumentList $arguments -Wait -PassThru
    $process.Refresh()
    $succeeded = (Test-Path -LiteralPath $log -PathType Leaf) -and
        [bool](Select-String -LiteralPath $log -SimpleMatch "Installation process succeeded.")
    if ($process.ExitCode -eq 0 -or $succeeded) {
        throw "Setup did not fail after receiver preservation for $Architecture."
    }
    if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
        throw "Forced receiver-preservation failure did not create a Setup log."
    }
    return $log
}

function Invoke-ForcedArchitectureMarkerFailure {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("Win32", "x64")]
        [string]$Architecture,
        [Parameter(Mandatory = $true)]
        [string]$InstallDirectory
    )

    $script:setupInvocation++
    $log = Join-Path $root ("setup-{0}-forced-marker-rollback-{1}.log" -f
        $Architecture, $script:setupInvocation)
    $arguments = @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS",
        "/ARCH=$Architecture", "/TESTFAILARCHITECTUREMARKERWRITE=1",
        ('/DIR="' + $InstallDirectory + '"'), ('/LOG="' + $log + '"')
    )
    $process = Start-Process -FilePath $setupPath -ArgumentList $arguments -Wait -PassThru
    $process.Refresh()
    $succeeded = (Test-Path -LiteralPath $log -PathType Leaf) -and
        [bool](Select-String -LiteralPath $log -SimpleMatch "Installation process succeeded.")
    if ($process.ExitCode -eq 0 -or $succeeded) {
        throw "Setup did not fail when the architecture marker could not be written for $Architecture."
    }
    if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
        throw "Forced architecture-marker failure did not create a Setup log."
    }
    return $log
}

function Invoke-FinalDirectoryArchitectureRefresh {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallDirectory
    )

    $script:setupInvocation++
    $log = Join-Path $root ("setup-final-directory-architecture-refresh-{0}.log" -f
        $script:setupInvocation)
    $arguments = @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS",
        "/TESTFINALDIRARCHITECTUREREFRESH=1",
        ('/DIR="' + $InstallDirectory + '"'), ('/LOG="' + $log + '"')
    )
    $process = Start-Process -FilePath $setupPath -ArgumentList $arguments -Wait -PassThru
    $process.Refresh()
    $succeeded = (Test-Path -LiteralPath $log -PathType Leaf) -and
        [bool](Select-String -LiteralPath $log -SimpleMatch "Installation process succeeded.")
    if ($process.ExitCode -ne 0 -or -not $succeeded) {
        throw "Setup failed while simulating a final-directory architecture-page refresh."
    }
    return $log
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

function Assert-InstalledApplication {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("Win32", "x64")]
        [string]$Architecture,
        [Parameter(Mandatory = $true)]
        [uint16]$ExpectedMachine,
        [Parameter(Mandatory = $true)]
        [string]$InstallDirectory
    )

    $executable = Join-Path $InstallDirectory "$displayName.exe"
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
    $buildMarker = Join-Path $InstallDirectory "PDW_BUILD_COMMIT.txt"
    if (-not (Test-Path -LiteralPath $buildMarker -PathType Leaf)) {
        throw "Installed build provenance marker is missing for $Architecture."
    }
    $actualBuildMarker = (Get-Content -LiteralPath $buildMarker -Raw).
        Replace("`r`n", "`n").Replace("`r", "`n")
    if ($actualBuildMarker -cne $expectedBuildMarker) {
        throw "Installed build provenance does not match exact clean commit $sourceCommit for $Architecture."
    }

    $requiredRuntimeNames = @(Get-RequiredAppLocalRuntimeNames $Architecture)
    $rootDlls = @(Get-ChildItem -LiteralPath $InstallDirectory -File -Filter *.dll)
    $runtimeLike = @($rootDlls | Where-Object {
        $_.Name -match '^(?i:(?:concrt|msvcp|vccorlib|vcruntime)[0-9][a-z0-9_]*|ucrtbase|api-ms-win-crt-[a-z0-9_-]+)\.dll$'
    })
    $unexpectedRuntime = @($runtimeLike | Where-Object {
        $requiredRuntimeNames -notcontains $_.Name
    })
    $missingRuntime = @($requiredRuntimeNames | Where-Object {
        $runtimeLike.Name -notcontains $_
    })
    if ($unexpectedRuntime -or $missingRuntime) {
        throw ("Installed {0} runtime set is not exact. Missing: [{1}]. Extra: [{2}]." -f
            $Architecture, ($missingRuntime -join ', '), ($unexpectedRuntime.Name -join ', '))
    }
    foreach ($runtimeName in $requiredRuntimeNames) {
        $runtime = $runtimeLike | Where-Object Name -IEQ $runtimeName | Select-Object -First 1
        $runtimeMachine = Get-PeMachine $runtime.FullName
        if ($runtimeMachine -ne $ExpectedMachine) {
            throw ("Installed runtime {0} has wrong {1} PE machine 0x{2:X4}." -f
                $runtime.Name, $Architecture, $runtimeMachine)
        }
        if ($runtime.VersionInfo.FileVersion -notmatch '^14\.5[0-9]\.' -or
            -not [string]::Equals($runtime.VersionInfo.OriginalFilename, $runtime.Name,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Installed runtime $($runtime.Name) is not a validated Microsoft VC145 release DLL."
        }
    }
    foreach ($relative in @("PDW.INI", "Receivers", "Wavfiles")) {
        if (-not (Test-Path -LiteralPath (Join-Path $InstallDirectory $relative))) {
            throw "Co-located installer content is missing for $Architecture $relative."
        }
    }
    foreach ($forbidden in @(
        "PDW-Adelaide-FLEX.INI",
        "PDW_INSTALLER_INPUT_SHA256SUMS.txt"
    )) {
        if (Test-Path -LiteralPath (Join-Path $InstallDirectory $forbidden) -PathType Leaf) {
            throw "Fresh installation deployed forbidden file $forbidden for $Architecture."
        }
    }
    $freshFilters = Get-ChildItem -LiteralPath $InstallDirectory -Recurse -Force -File |
        Where-Object { $_.Name -ieq "filters.ini" }
    if ($freshFilters) {
        throw "Fresh installation deployed retired filters.ini for $Architecture."
    }
    if ($Architecture -eq "x64" -and
        (Test-Path -LiteralPath (Join-Path $InstallDirectory "Receivers\RTL-SDR\rtlsdr.dll"))) {
        throw "The x64 installation contains the x86-only RTL-SDR DLL."
    }
    if ($Architecture -eq "Win32" -and
        -not (Test-Path -LiteralPath (Join-Path $InstallDirectory "Receivers\RTL-SDR\rtlsdr.dll"))) {
        throw "The Win32 installation is missing the bundled x86 RTL-SDR DLL."
    }
    return $version
}

function Assert-InvalidProfileRejected {
    $installDirectory = Join-Path $root "invalid-profile"
    $log = Join-Path $root "setup-invalid-profile.log"
    $arguments = @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS",
        "/ARCH=Win32", "/PROFILE=not-a-profile",
        ('/DIR="' + $installDirectory + '"'), ('/LOG="' + $log + '"')
    )
    $process = Start-Process -FilePath $setupPath -ArgumentList $arguments -Wait -PassThru
    $succeeded = (Test-Path -LiteralPath $log -PathType Leaf) -and
        [bool](Select-String -LiteralPath $log -SimpleMatch "Installation process succeeded.")
    if ($process.ExitCode -eq 0 -or $succeeded -or
        (Test-Path -LiteralPath (Join-Path $installDirectory "$displayName.exe") -PathType Leaf)) {
        throw "Setup accepted an invalid /PROFILE value."
    }
}

function Assert-InvalidArchitectureRejected {
    $installDirectory = Join-Path $root "invalid-architecture"
    $log = Join-Path $root "setup-invalid-architecture.log"
    $arguments = @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS",
        "/ARCH=arm64", "/PROFILE=standard",
        ('/DIR="' + $installDirectory + '"'), ('/LOG="' + $log + '"')
    )
    $process = Start-Process -FilePath $setupPath -ArgumentList $arguments -Wait -PassThru
    $succeeded = (Test-Path -LiteralPath $log -PathType Leaf) -and
        [bool](Select-String -LiteralPath $log -SimpleMatch "Installation process succeeded.")
    if ($process.ExitCode -eq 0 -or $succeeded -or
        (Test-Path -LiteralPath (Join-Path $installDirectory "$displayName.exe") -PathType Leaf)) {
        throw "Setup accepted an invalid /ARCH value."
    }
}

function Test-Architecture([string]$Architecture, [uint16]$ExpectedMachine) {
    $standardDirectory = Join-Path $root "$Architecture-standard"
    Invoke-Setup $Architecture $standardDirectory
    $standardVersion = Assert-InstalledApplication $Architecture $ExpectedMachine $standardDirectory
    $standardIni = Join-Path $standardDirectory "PDW.INI"
    Assert-FileHash $standardIni $standardProfileHash `
        "Clean Standard install did not use the packaged PDW.INI for $Architecture."
    Invoke-Setup $Architecture $standardDirectory -UseInstalledArchitecture
    [void](Assert-InstalledApplication $Architecture $ExpectedMachine $standardDirectory)

    Copy-Item -LiteralPath $preserveFixture -Destination $standardIni -Force
    $legacyFilters = Join-Path $standardDirectory "filters.ini"
    $capcodeDatabase = Join-Path $standardDirectory "pdw-history.sqlite3"
    $customReceiver = Join-Path $standardDirectory "Receivers\custom-receiver.ini"
    Set-Content -LiteralPath $legacyFilters -Value "[Filter]`r`n`r`nFilterCount=0`r`n" -Encoding Ascii
    Set-Content -LiteralPath $capcodeDatabase -Value "synthetic Capcode Directory database" -Encoding Ascii
    Set-Content -LiteralPath $customReceiver -Value "[Receiver]`r`nName=Installer smoke custom receiver" -Encoding Ascii
    $operatorHashes = @{
        $standardIni = (Get-FileHash -LiteralPath $standardIni -Algorithm SHA256).Hash
        $legacyFilters = (Get-FileHash -LiteralPath $legacyFilters -Algorithm SHA256).Hash
        $capcodeDatabase = (Get-FileHash -LiteralPath $capcodeDatabase -Algorithm SHA256).Hash
        $customReceiver = (Get-FileHash -LiteralPath $customReceiver -Algorithm SHA256).Hash
    }
    $predecessorExecutables = @(
        "PDW v5 2026 Release.exe",
        "PDW v5.1 2026 Release.exe",
        "PDW v5.2 2026 Release.exe",
        "PDW v5.3 2026 Release.exe",
        "PDW v5.4 2026 Release.exe",
        "PDW v5.5 2026 Release.exe",
        "PDW v5.5.1 2026 Release.exe"
    ) | ForEach-Object { Join-Path $standardDirectory $_ }
    foreach ($predecessorExecutable in $predecessorExecutables) {
        Set-Content -LiteralPath $predecessorExecutable -Value "synthetic predecessor executable" -Encoding Ascii
    }

    Invoke-Setup $Architecture $standardDirectory "adelaide-flex"
    foreach ($path in $operatorHashes.Keys) {
        Assert-FileHash $path $operatorHashes[$path] "Upgrade changed operator data for $Architecture."
    }
    $unexpectedFilters = Get-ChildItem -LiteralPath $standardDirectory -Recurse -Force -File |
        Where-Object { $_.Name -ieq "filters.ini" -and $_.FullName -ne $legacyFilters }
    if ($unexpectedFilters) {
        throw "Upgrade deployed an additional filters.ini for $Architecture."
    }
    foreach ($predecessorExecutable in $predecessorExecutables) {
        if (Test-Path -LiteralPath $predecessorExecutable -PathType Leaf) {
            throw "Upgrade left predecessor executable $predecessorExecutable for $Architecture."
        }
    }

    Invoke-Uninstall $standardDirectory
    Assert-AppOwnedFilesRemoved $standardDirectory $Architecture
    foreach ($path in $operatorHashes.Keys) {
        Assert-FileHash $path $operatorHashes[$path] "Uninstall changed operator data for $Architecture."
    }

    $adelaideDirectory = Join-Path $root "$Architecture-adelaide-flex"
    Invoke-Setup $Architecture $adelaideDirectory "adelaide-flex"
    [void](Assert-InstalledApplication $Architecture $ExpectedMachine $adelaideDirectory)
    $adelaideIni = Join-Path $adelaideDirectory "PDW.INI"
    Assert-FileHash $adelaideIni $adelaideProfileHash `
        "Clean Adelaide profile install used the wrong PDW.INI for $Architecture."
    $namedDatabase = Join-Path $adelaideDirectory "pdw-history.sqlite3"
    Set-Content -LiteralPath $namedDatabase -Value "synthetic named-profile Capcode Directory" -Encoding Ascii
    $namedDatabaseHash = (Get-FileHash -LiteralPath $namedDatabase -Algorithm SHA256).Hash

    Invoke-Setup $Architecture $adelaideDirectory "standard"
    Assert-FileHash $adelaideIni $adelaideProfileHash `
        "Opposite-profile reinstall replaced the Adelaide PDW.INI for $Architecture."
    Assert-FileHash $namedDatabase $namedDatabaseHash `
        "Opposite-profile reinstall changed the Capcode Directory for $Architecture."
    Invoke-Uninstall $adelaideDirectory
    Assert-AppOwnedFilesRemoved $adelaideDirectory $Architecture
    Assert-FileHash $adelaideIni $adelaideProfileHash `
        "Uninstall changed the Adelaide PDW.INI for $Architecture."
    Assert-FileHash $namedDatabase $namedDatabaseHash `
        "Uninstall changed the Capcode Directory for $Architecture."

    [pscustomobject]@{
        Architecture = $Architecture
        Machine = ("0x{0:X4}" -f $ExpectedMachine)
        Version = $standardVersion.ProductVersion
        StandardProfile = "Passed"
        AdelaideProfile = "Passed"
        DefaultUpgradeArchitecture = "Passed"
        UpgradePreservation = "Passed"
        CapcodeDirectoryPreservation = "Passed"
        LegacyFilterPreservation = "Passed"
        UninstallPreservation = "Passed"
    }
}

function Test-CrossArchitectureReceiverBackup {
    if (-not [Environment]::Is64BitOperatingSystem) { return }
    $directory = Join-Path $root "cross-architecture"
    Invoke-Setup "x64" $directory
    [void](Assert-InstalledApplication "x64" 0x8664 $directory)
    $ini = Join-Path $directory "PDW.INI"
    $customReceiver = Join-Path $directory "Receivers\cross-architecture-custom.ini"
    Set-Content -LiteralPath $customReceiver -Value "[Receiver]`r`nName=preserve me" -Encoding Ascii
    $operatorHashes = @{
        $ini = (Get-FileHash -LiteralPath $ini -Algorithm SHA256).Hash
        $customReceiver = (Get-FileHash -LiteralPath $customReceiver -Algorithm SHA256).Hash
    }

    $receiverDll = Join-Path $directory "Receivers\RTL-SDR\rtlsdr.dll"
    Copy-Item -LiteralPath (Join-Path $directory "$displayName.exe") -Destination $receiverDll
    $x64ReceiverHash = (Get-FileHash -LiteralPath $receiverDll -Algorithm SHA256).Hash
    Invoke-Setup "Win32" $directory
    [void](Assert-InstalledApplication "Win32" 0x014c $directory)
    $x64Backup = Get-ChildItem -LiteralPath (Split-Path -Parent $receiverDll) -File |
        Where-Object Name -Like "rtlsdr.dll.pre-x64-architecture*.bak" |
        Select-Object -First 1
    if (-not $x64Backup) { throw "Cross-architecture upgrade did not preserve the x64 receiver DLL." }
    Assert-FileHash $x64Backup.FullName $x64ReceiverHash `
        "Cross-architecture x64 receiver backup changed content."
    if ((Get-PeMachine $receiverDll) -ne 0x014c) {
        throw "Cross-architecture Win32 upgrade did not install an x86 RTL-SDR DLL."
    }
    $win32ReceiverHash = (Get-FileHash -LiteralPath $receiverDll -Algorithm SHA256).Hash

    Invoke-Setup "x64" $directory
    [void](Assert-InstalledApplication "x64" 0x8664 $directory)
    $win32Backup = Get-ChildItem -LiteralPath (Split-Path -Parent $receiverDll) -File |
        Where-Object Name -Like "rtlsdr.dll.pre-win32-architecture*.bak" |
        Select-Object -First 1
    if (-not $win32Backup) { throw "Cross-architecture upgrade did not preserve the x86 receiver DLL." }
    Assert-FileHash $win32Backup.FullName $win32ReceiverHash `
        "Cross-architecture Win32 receiver backup changed content."
    foreach ($path in $operatorHashes.Keys) {
        Assert-FileHash $path $operatorHashes[$path] "Cross-architecture upgrade changed operator data."
    }
    Invoke-Uninstall $directory
    Assert-AppOwnedFilesRemoved $directory "x64 cross-architecture"
    Assert-FileHash $x64Backup.FullName $x64ReceiverHash `
        "Uninstall removed or changed the retained x64 receiver recovery copy."
    Assert-FileHash $win32Backup.FullName $win32ReceiverHash `
        "Uninstall removed or changed the retained Win32 receiver recovery copy."
}

function Test-CrossArchitectureReceiverRollback {
    if (-not [Environment]::Is64BitOperatingSystem) { return }
    $directory = Join-Path $root "cross-architecture-rollback"
    Invoke-Setup "x64" $directory
    [void](Assert-InstalledApplication "x64" 0x8664 $directory)

    $receiverDll = Join-Path $directory "Receivers\RTL-SDR\rtlsdr.dll"
    Copy-Item -LiteralPath (Join-Path $directory "$displayName.exe") -Destination $receiverDll
    $receiverHash = (Get-FileHash -LiteralPath $receiverDll -Algorithm SHA256).Hash
    $ini = Join-Path $directory "PDW.INI"
    $marker = Join-Path $directory "installation-architecture.txt"
    $legacyFilters = Join-Path $directory "filters.ini"
    $capcodeDatabase = Join-Path $directory "pdw-history.sqlite3"
    $customReceiver = Join-Path $directory "Receivers\rollback-custom.ini"
    Set-Content -LiteralPath $legacyFilters -Value "[Filter]`r`nFilterCount=0`r`n" -Encoding Ascii
    Set-Content -LiteralPath $capcodeDatabase -Value "synthetic rollback Capcode Directory" -Encoding Ascii
    Set-Content -LiteralPath $customReceiver -Value "[Receiver]`r`nName=rollback preserve me" -Encoding Ascii
    $operatorHashes = @{
        $ini = (Get-FileHash -LiteralPath $ini -Algorithm SHA256).Hash
        $marker = (Get-FileHash -LiteralPath $marker -Algorithm SHA256).Hash
        $legacyFilters = (Get-FileHash -LiteralPath $legacyFilters -Algorithm SHA256).Hash
        $capcodeDatabase = (Get-FileHash -LiteralPath $capcodeDatabase -Algorithm SHA256).Hash
        $customReceiver = (Get-FileHash -LiteralPath $customReceiver -Algorithm SHA256).Hash
    }

    $failureLog = Invoke-ForcedReceiverPreservationFailure "Win32" $directory
    Assert-FileHash $receiverDll $receiverHash `
        "Failed cross-architecture install did not restore the active receiver DLL byte-for-byte."
    foreach ($path in $operatorHashes.Keys) {
        Assert-FileHash $path $operatorHashes[$path] `
            "Failed cross-architecture install changed operator data."
    }
    $rollbackBackups = @(Get-ChildItem -LiteralPath (Split-Path -Parent $receiverDll) -File |
        Where-Object Name -Like "rtlsdr.dll.pre-x64-architecture*.bak")
    if ($rollbackBackups.Count -ne 0) {
        throw "Failed architecture switch left a temporary receiver recovery copy after verified restore."
    }
    if (-not (Select-String -LiteralPath $failureLog -SimpleMatch `
        "Restored cross-architecture receiver DLL after incomplete installation")) {
        throw "Forced architecture-switch failure log does not confirm verified receiver restoration."
    }
    if ((Get-PeMachine (Join-Path $directory "$displayName.exe")) -ne 0x8664) {
        throw "Failed architecture switch changed the installed application architecture."
    }
    Invoke-Uninstall $directory
}

function Test-ArchitectureMarkerWriteRollback {
    if (-not [Environment]::Is64BitOperatingSystem) { return }
    $directory = Join-Path $root "architecture-marker-rollback"
    Invoke-Setup "x64" $directory
    [void](Assert-InstalledApplication "x64" 0x8664 $directory)

    $application = Join-Path $directory "$displayName.exe"
    $receiverDll = Join-Path $directory "Receivers\RTL-SDR\rtlsdr.dll"
    Copy-Item -LiteralPath $application -Destination $receiverDll
    $ini = Join-Path $directory "PDW.INI"
    $marker = Join-Path $directory "installation-architecture.txt"
    $legacyFilters = Join-Path $directory "filters.ini"
    $capcodeDatabase = Join-Path $directory "pdw-history.sqlite3"
    $customReceiver = Join-Path $directory "Receivers\marker-rollback-custom.ini"
    Set-Content -LiteralPath $legacyFilters -Value "[Filter]`r`nFilterCount=0`r`n" -Encoding Ascii
    Set-Content -LiteralPath $capcodeDatabase -Value "synthetic marker rollback Capcode Directory" -Encoding Ascii
    Set-Content -LiteralPath $customReceiver -Value "[Receiver]`r`nName=marker rollback preserve me" -Encoding Ascii
    $preservedHashes = @{
        $application = (Get-FileHash -LiteralPath $application -Algorithm SHA256).Hash
        $receiverDll = (Get-FileHash -LiteralPath $receiverDll -Algorithm SHA256).Hash
        $ini = (Get-FileHash -LiteralPath $ini -Algorithm SHA256).Hash
        $marker = (Get-FileHash -LiteralPath $marker -Algorithm SHA256).Hash
        $legacyFilters = (Get-FileHash -LiteralPath $legacyFilters -Algorithm SHA256).Hash
        $capcodeDatabase = (Get-FileHash -LiteralPath $capcodeDatabase -Algorithm SHA256).Hash
        $customReceiver = (Get-FileHash -LiteralPath $customReceiver -Algorithm SHA256).Hash
    }

    $failureLog = Invoke-ForcedArchitectureMarkerFailure "Win32" $directory
    foreach ($path in $preservedHashes.Keys) {
        Assert-FileHash $path $preservedHashes[$path] `
            "Architecture-marker failure did not restore installed and operator files byte-for-byte."
    }
    if ((Get-PeMachine $application) -ne 0x8664) {
        throw "Architecture-marker failure changed the installed application architecture."
    }
    $receiverBackups = @(Get-ChildItem -LiteralPath (Split-Path -Parent $receiverDll) -File |
        Where-Object Name -Like "rtlsdr.dll.pre-x64-architecture*.bak")
    if ($receiverBackups.Count -ne 0) {
        throw "Architecture-marker failure left a temporary receiver recovery copy after verified restore."
    }
    $markerBackups = @(Get-ChildItem -LiteralPath $directory -Force -File |
        Where-Object Name -Like "installation-architecture.txt.pdw-setup-rollback*.bak")
    if ($markerBackups.Count -ne 0) {
        throw "Architecture-marker failure left a temporary marker recovery copy after verified restore."
    }
    foreach ($expectedLogText in @(
        "PDW could not save and verify installation-architecture.txt.",
        "Restored installation-architecture marker after incomplete installation"
    )) {
        if (-not (Select-String -LiteralPath $failureLog -SimpleMatch $expectedLogText)) {
            throw "Forced architecture-marker failure log is missing: $expectedLogText"
        }
    }
    Invoke-Uninstall $directory
}

function Test-CleanArchitectureMarkerWriteFailure {
    $directory = Join-Path $root "clean-marker-write-failure"
    $failureLog = Invoke-ForcedArchitectureMarkerFailure "x64" $directory
    if (Test-Path -LiteralPath $directory) {
        throw "Clean marker-write failure left the temporary installation directory behind."
    }
    foreach ($expectedLogText in @(
        "PDW could not save and verify installation-architecture.txt.",
        "Removed installation-architecture marker created by incomplete installation",
        "Removed empty installation directory created for the incomplete Setup"
    )) {
        if (-not (Select-String -LiteralPath $failureLog -SimpleMatch $expectedLogText)) {
            throw "Clean marker-write failure log is missing: $expectedLogText"
        }
    }
}

function Test-FinalDirectoryArchitectureRefresh {
    if (-not [Environment]::Is64BitOperatingSystem) { return }
    $directory = Join-Path $root "final-directory-architecture-refresh"
    Invoke-Setup "Win32" $directory
    [void](Assert-InstalledApplication "Win32" 0x014c $directory)
    $marker = Join-Path $directory "installation-architecture.txt"
    $markerHash = (Get-FileHash -LiteralPath $marker -Algorithm SHA256).Hash

    $refreshLog = Invoke-FinalDirectoryArchitectureRefresh $directory
    [void](Assert-InstalledApplication "Win32" 0x014c $directory)
    Assert-FileHash $marker $markerHash `
        "Final-directory architecture refresh did not preserve the browsed-to Win32 selection."
    $refreshMatches = @(Select-String -LiteralPath $refreshLog -SimpleMatch `
        "Architecture selection refreshed from final installation folder")
    if ($refreshMatches.Count -lt 2) {
        throw "Final-directory smoke did not exercise initialization and simulated page-entry refreshes."
    }
    if (-not (Select-String -LiteralPath $refreshLog -SimpleMatch `
        (('Architecture selection refreshed from final installation folder "{0}": win32' -f
            $directory)))) {
        throw "Final-directory page refresh did not resolve the selected directory's Win32 marker."
    }
    Invoke-Uninstall $directory
}

Test-Architecture "x64" 0x8664
Test-Architecture "Win32" 0x014c
Test-CrossArchitectureReceiverRollback
Test-ArchitectureMarkerWriteRollback
Test-CleanArchitectureMarkerWriteFailure
Test-FinalDirectoryArchitectureRefresh
Test-CrossArchitectureReceiverBackup
Assert-InvalidProfileRejected
Assert-InvalidArchitectureRejected
