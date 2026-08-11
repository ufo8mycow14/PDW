[CmdletBinding()]
param(
    [ValidateSet("x86", "x64")]
    [string]$Architecture = "x86",
    [ValidateSet(17, 18)]
    [int]$VisualStudioMajor = 18,
    [string]$InstallRoot = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "out"))
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = Join-Path $outRoot "dependencies\$Architecture"
}
$InstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)
$cmakePlatform = if ($Architecture -eq "x64") { "x64" } else { "Win32" }
$opensslTarget = if ($Architecture -eq "x64") { "VC-WIN64A" } else { "VC-WIN32" }

function Assert-PathUnderOut {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $outPrefix = $outRoot.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($outPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Dependency build paths must stay under '$outRoot'. Refusing '$fullPath'."
    }
}

Assert-PathUnderOut -Path $InstallRoot

$programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer's vswhere.exe was not found. Install Visual Studio 2026 Desktop development with C++."
}

$versionRange = "[$VisualStudioMajor.0,$($VisualStudioMajor + 1).0)"
$visualStudioRoot = (& $vswhere -latest -products * -version $versionRange `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($visualStudioRoot)) {
    throw "Visual Studio $VisualStudioMajor C++ x86/x64 build tools were not found."
}

$toolsetVersionFile = Join-Path $visualStudioRoot "VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
if (-not (Test-Path -LiteralPath $toolsetVersionFile -PathType Leaf)) {
    throw "The default MSVC toolset version file was not found at '$toolsetVersionFile'."
}
$msvcToolsetVersion = (Get-Content -LiteralPath $toolsetVersionFile -Raw).Trim()
$cmakeToolset = if ($VisualStudioMajor -eq 18) { "v145" } else { "v143" }
$resolverPath = Join-Path $PSScriptRoot "resolve-cmake-generator.ps1"
$cmakeGenerator = & $resolverPath `
    -VisualStudioMajor $VisualStudioMajor
$cmakeCommand = Get-Command cmake.exe -ErrorAction Stop
$cmakeExecutable = $cmakeCommand.Source
$cmakeVersionOutput = @(& $cmakeExecutable --version)
$cmakeVersionExitCode = $LASTEXITCODE
$cmakeVersionLine = ($cmakeVersionOutput | Select-Object -First 1)
if ($cmakeVersionExitCode -ne 0 -or $cmakeVersionLine -notmatch '^cmake version\s+([^\s]+)') {
    throw "The selected CMake executable did not report a valid version."
}
$cmakeVersion = $Matches[1]
$resolverSha256 = (Get-FileHash -LiteralPath $resolverPath -Algorithm SHA256).Hash
Write-Host "Using CMake $cmakeVersion generator '$cmakeGenerator' and MSVC $msvcToolsetVersion ($cmakeToolset)."

$recipeSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
$versions = [ordered]@{
    openssl = "3.5.7"
    curl = "8.21.0"
    libssh2 = "1.11.1"
    architecture = $Architecture
    visualStudioMajor = $VisualStudioMajor
    msvcToolset = $msvcToolsetVersion
    cmakeToolset = $cmakeToolset
    cmakeGenerator = $cmakeGenerator
    cmakeVersion = $cmakeVersion
    resolverSha256 = $resolverSha256
    recipeSha256 = $recipeSha256
}

$archives = @(
    @{
        Name = "openssl-3.5.7.tar.gz"
        Uri = "https://github.com/openssl/openssl/releases/download/openssl-3.5.7/openssl-3.5.7.tar.gz"
        Sha256 = "A8C0D28A529CA480F9F36CF5792E2CD21984552A3C8E4AA11A24AA31AEAC98E8"
    },
    @{
        Name = "libssh2-1.11.1.tar.xz"
        Uri = "https://www.libssh2.org/download/libssh2-1.11.1.tar.xz"
        Sha256 = "9954CB54C4F548198A7CBEBAD248BDC87DD64BD26185708A294B2B50771E3769"
    },
    @{
        Name = "curl-8.21.0.tar.xz"
        Uri = "https://curl.se/download/curl-8.21.0.tar.xz"
        Sha256 = "AA1B66A70EACE83DC624508745646C08AE561DE512AB403ADFFB93AC87FC72E6"
    }
)

$expectedMarker = ($versions.GetEnumerator() | ForEach-Object { "{0}={1}" -f $_.Key, $_.Value }) -join "`n"
$markerPath = Join-Path $InstallRoot "pdw-dependencies.lock"
$requiredFiles = @(
    (Join-Path $InstallRoot "include\openssl\opensslv.h"),
    (Join-Path $InstallRoot "include\curl\curl.h"),
    (Join-Path $InstallRoot "lib\libcrypto.lib"),
    (Join-Path $InstallRoot "lib\libssl.lib"),
    (Join-Path $InstallRoot "lib\libcurl.lib"),
    (Join-Path $InstallRoot "lib\libssh2.lib"),
    (Join-Path $InstallRoot "licenses\OpenSSL-Apache-2.0.txt"),
    (Join-Path $InstallRoot "licenses\curl.txt"),
    (Join-Path $InstallRoot "licenses\libssh2.txt")
)

$dependencySetIsCurrent = -not $Force -and (Test-Path -LiteralPath $markerPath)
if ($dependencySetIsCurrent) {
    $dependencySetIsCurrent = ((Get-Content -LiteralPath $markerPath -Raw).Trim() -eq $expectedMarker.Trim())
}
if ($dependencySetIsCurrent) {
    foreach ($requiredFile in $requiredFiles) {
        if (-not (Test-Path -LiteralPath $requiredFile)) {
            $dependencySetIsCurrent = $false
            break
        }
    }
}

if ($dependencySetIsCurrent) {
    Write-Host "PDW dependencies are already current in '$InstallRoot'."
    exit 0
}

$tarExecutable = Join-Path $env:SystemRoot "System32\tar.exe"
if (-not (Test-Path -LiteralPath $tarExecutable -PathType Leaf)) {
    throw "Required Windows build tool '$tarExecutable' was not found. See Readme > Build on Windows."
}

$perlCandidates = @()
$currentPerl = Get-Command perl.exe -ErrorAction SilentlyContinue
if ($currentPerl) {
    $perlCandidates += $currentPerl.Source
}
$perlCandidates += @(
    "C:\Strawberry\perl\bin\perl.exe",
    (Join-Path ([Environment]::GetFolderPath("ProgramFiles")) "Git\usr\bin\perl.exe")
)
$perlExecutable = ""
foreach ($candidate in ($perlCandidates | Select-Object -Unique)) {
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        continue
    }
    & $candidate -MLocale::Maketext::Simple -e "1" 2>$null
    if ($LASTEXITCODE -eq 0) {
        $perlExecutable = $candidate
        break
    }
}
if ([string]::IsNullOrWhiteSpace($perlExecutable)) {
    throw "A Perl installation with Locale::Maketext::Simple was not found. See Readme > Build on Windows."
}
$env:Path = "$(Split-Path -Parent $perlExecutable);$env:Path"

$vsDevCmd = Join-Path $visualStudioRoot "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command script was not found at '$vsDevCmd'."
}

# CMake's Visual Studio generator still probes compiler/binutils from the
# process environment on this toolchain. Import the exact target developer
# environment once so every direct CMake configure/build uses the same MSVC
# instance as the OpenSSL nmake build below.
$environmentCommand = "call `"$vsDevCmd`" -no_logo -arch=$Architecture -host_arch=x64 >nul && set"
$environmentLines = & $env:ComSpec /d /s /c $environmentCommand
$environmentExitCode = $LASTEXITCODE
if ($environmentExitCode -ne 0) {
    throw "Visual Studio developer environment initialization failed with exit code $environmentExitCode."
}
$developerPathLine = $environmentLines |
    Where-Object {
        $_ -match '^(?i:path)=' -and
        $_ -like "*$visualStudioRoot\VC\Tools\MSVC*"
    } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($developerPathLine)) {
    throw "Visual Studio developer environment did not return a canonical PATH."
}
$developerPath = $developerPathLine.Substring($developerPathLine.IndexOf('=') + 1)
foreach ($environmentLine in $environmentLines) {
    if ($environmentLine -match '^([^=][^=]*)=(.*)$') {
        if (-not [string]::Equals($Matches[1], 'Path', [StringComparison]::OrdinalIgnoreCase)) {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
}
$processEnvironment = [Environment]::GetEnvironmentVariables('Process')
foreach ($entry in $processEnvironment.GetEnumerator()) {
    if ([string]::Equals([string]$entry.Key, 'Path', [StringComparison]::OrdinalIgnoreCase)) {
        [Environment]::SetEnvironmentVariable([string]$entry.Key, $null, 'Process')
    }
}
[Environment]::SetEnvironmentVariable(
    'Path',
    "$(Split-Path -Parent $perlExecutable);$developerPath",
    'Process'
)
$validatedGenerator = & $resolverPath -VisualStudioMajor $VisualStudioMajor
if ($validatedGenerator -ne $cmakeGenerator) {
    throw "The Visual Studio developer environment changed the selected CMake generator."
}

function Invoke-VsDevCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Write-Host $Description
    $developerCommand = "call `"$vsDevCmd`" -no_logo -arch=$Architecture -host_arch=x64 && $Command"
    & $env:ComSpec /d /s /c $developerCommand
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Invoke-CMake {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Write-Host $Description
    & $cmakeExecutable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Invoke-VerifiedDownload {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256,
        [Parameter(Mandatory = $true)][string]$DisplayName,
        [ValidateRange(1, 6)][int]$MaximumAttempts = 4
    )

    $partial = "$Destination.partial"
    for ($attempt = 1; $attempt -le $MaximumAttempts; $attempt++) {
        if (Test-Path -LiteralPath $partial) {
            Remove-Item -LiteralPath $partial -Force
        }
        try {
            Write-Host "Downloading $DisplayName (attempt $attempt of $MaximumAttempts)..."
            Invoke-WebRequest -Uri $Uri -OutFile $partial
            $actualHash = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash
            if ($actualHash -ne $ExpectedSha256) {
                throw "Checksum mismatch. Expected $ExpectedSha256, received $actualHash."
            }
            Move-Item -LiteralPath $partial -Destination $Destination
            return
        }
        catch {
            if (Test-Path -LiteralPath $partial) {
                Remove-Item -LiteralPath $partial -Force
            }
            if ($attempt -eq $MaximumAttempts) {
                throw "Unable to download and verify '$DisplayName' after $MaximumAttempts attempts: $($_.Exception.Message)"
            }
            Start-Sleep -Seconds ([Math]::Min(8, 2 * $attempt))
        }
    }
}

$cacheRoot = Join-Path $outRoot "dependency-cache"
$sourceRoot = Join-Path $outRoot "dependency-sources\$Architecture"
$buildRoot = Join-Path $outRoot "dependency-build\$Architecture"
foreach ($path in @($cacheRoot, $sourceRoot, $buildRoot, $InstallRoot)) {
    Assert-PathUnderOut -Path $path
}

foreach ($path in @($sourceRoot, $buildRoot, $InstallRoot)) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}
New-Item -ItemType Directory -Path $cacheRoot, $sourceRoot, $buildRoot, $InstallRoot -Force | Out-Null

foreach ($archive in $archives) {
    $archivePath = Join-Path $cacheRoot $archive.Name
    $downloadRequired = $true
    if (Test-Path -LiteralPath $archivePath) {
        $downloadRequired = ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $archive.Sha256)
    }

    if ($downloadRequired) {
        if (Test-Path -LiteralPath $archivePath) {
            Remove-Item -LiteralPath $archivePath -Force
        }
        Invoke-VerifiedDownload -Uri $archive.Uri -Destination $archivePath `
            -ExpectedSha256 $archive.Sha256 -DisplayName $archive.Name
    }

    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($actualHash -ne $archive.Sha256) {
        throw "Checksum verification failed for '$($archive.Name)'. Expected $($archive.Sha256), received $actualHash."
    }

    Write-Host "Extracting $($archive.Name)..."
    & $tarExecutable -xf $archivePath -C $sourceRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Extraction failed for '$($archive.Name)'."
    }
}

$opensslSource = Join-Path $sourceRoot "openssl-$($versions.openssl)"
$libssh2Source = Join-Path $sourceRoot "libssh2-$($versions.libssh2)"
$curlSource = Join-Path $sourceRoot "curl-$($versions.curl)"

$opensslCommand = "cd /d `"$opensslSource`" && `"$perlExecutable`" Configure $opensslTarget no-shared no-module no-tests no-apps no-docs no-asm --prefix=`"$InstallRoot`" --openssldir=`"$InstallRoot\ssl`" --libdir=lib && nmake /NOLOGO && nmake /NOLOGO install_sw"
Invoke-VsDevCommand -Command $opensslCommand -Description "Building OpenSSL $($versions.openssl) for $cmakePlatform"

$libssh2Build = Join-Path $buildRoot "libssh2"
Invoke-CMake -Description "Configuring libssh2 $($versions.libssh2) for $cmakePlatform" -Arguments @(
    "-S", $libssh2Source,
    "-B", $libssh2Build,
    "-G", $cmakeGenerator,
    "-A", $cmakePlatform,
    "-T", $cmakeToolset,
    "-DCMAKE_INSTALL_PREFIX=$InstallRoot",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DBUILD_STATIC_LIBS=ON",
    "-DCRYPTO_BACKEND=WinCNG",
    "-DENABLE_ECDSA_WINCNG=ON",
    "-DBUILD_EXAMPLES=OFF",
    "-DBUILD_TESTING=OFF"
)
Invoke-CMake -Description "Building and installing libssh2 $($versions.libssh2)" -Arguments @(
    "--build", $libssh2Build,
    "--config", "Release",
    "--target", "install",
    "--parallel"
)

$curlBuild = Join-Path $buildRoot "curl"
Invoke-CMake -Description "Configuring curl $($versions.curl) for $cmakePlatform" -Arguments @(
    "-S", $curlSource,
    "-B", $curlBuild,
    "-G", $cmakeGenerator,
    "-A", $cmakePlatform,
    "-T", $cmakeToolset,
    "-DCMAKE_INSTALL_PREFIX=$InstallRoot",
    "-DCMAKE_PREFIX_PATH=$InstallRoot",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DBUILD_STATIC_LIBS=ON",
    "-DBUILD_CURL_EXE=OFF",
    "-DBUILD_EXAMPLES=OFF",
    "-DBUILD_TESTING=OFF",
    "-DBUILD_LIBCURL_DOCS=OFF",
    "-DBUILD_MISC_DOCS=OFF",
    "-DENABLE_CURL_MANUAL=OFF",
    "-DCURL_USE_SCHANNEL=ON",
    "-DCURL_USE_OPENSSL=OFF",
    "-DCURL_USE_LIBSSH2=ON",
    "-DLIBSSH2_USE_STATIC_LIBS=ON",
    "-DCURL_USE_LIBSSH=OFF",
    "-DCURL_USE_LIBPSL=OFF",
    "-DCURL_ZLIB=OFF",
    "-DCURL_BROTLI=OFF",
    "-DCURL_ZSTD=OFF",
    "-DENABLE_ARES=OFF",
    "-DUSE_LIBIDN2=OFF",
    "-DUSE_WIN32_IDN=ON",
    "-DUSE_NGHTTP2=OFF",
    "-DUSE_NGTCP2=OFF",
    "-DUSE_QUICHE=OFF",
    "-DCURL_USE_GSASL=OFF",
    "-DCURL_USE_GSSAPI=OFF",
    "-DCURL_USE_LIBUV=OFF",
    "-DCURL_DISABLE_LDAP=ON",
    "-DCURL_DISABLE_LDAPS=ON",
    "-DHTTP_ONLY=OFF",
    "-DCURL_USE_CMAKECONFIG=ON"
)
Invoke-CMake -Description "Building and installing curl $($versions.curl)" -Arguments @(
    "--build", $curlBuild,
    "--config", "Release",
    "--target", "install",
    "--parallel"
)

$licenseRoot = Join-Path $InstallRoot "licenses"
New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $opensslSource "LICENSE.txt") -Destination (Join-Path $licenseRoot "OpenSSL-Apache-2.0.txt")
Copy-Item -LiteralPath (Join-Path $curlSource "COPYING") -Destination (Join-Path $licenseRoot "curl.txt")
Copy-Item -LiteralPath (Join-Path $libssh2Source "COPYING") -Destination (Join-Path $licenseRoot "libssh2.txt")

foreach ($requiredFile in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $requiredFile)) {
        throw "Dependency build completed without expected file '$requiredFile'."
    }
}

$opensslVersionHeader = Get-Content -LiteralPath (Join-Path $InstallRoot "include\openssl\opensslv.h") -Raw
if ($opensslVersionHeader -notmatch '#\s*define\s+OPENSSL_VERSION_STR\s+"3\.5\.7"') {
    throw "The installed OpenSSL headers do not report version 3.5.7."
}

Set-Content -LiteralPath $markerPath -Value $expectedMarker -Encoding ascii
Write-Host "PDW $cmakePlatform dependencies were installed successfully to '$InstallRoot'."
