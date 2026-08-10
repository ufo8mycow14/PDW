[CmdletBinding()]
param(
    [string]$InstallRoot = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "out"))
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = Join-Path $outRoot "dependencies\x86"
}
$InstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)

function Assert-PathUnderOut {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $outPrefix = $outRoot.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($outPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Dependency build paths must stay under '$outRoot'. Refusing '$fullPath'."
    }
}

Assert-PathUnderOut -Path $InstallRoot

$recipeSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
$versions = [ordered]@{
    openssl = "3.5.7"
    curl = "8.21.0"
    libssh2 = "1.11.1"
    architecture = "x86"
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

foreach ($commandName in @("cmake", "perl", "tar")) {
    if (-not (Get-Command $commandName -ErrorAction SilentlyContinue)) {
        throw "Required build tool '$commandName' was not found. See Readme > Build on Windows."
    }
}

$programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer's vswhere.exe was not found. Install Visual Studio 2022 Desktop development with C++."
}

$visualStudioRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($visualStudioRoot)) {
    throw "Visual Studio C++ x86/x64 build tools were not found."
}

$vsDevCmd = Join-Path $visualStudioRoot "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command script was not found at '$vsDevCmd'."
}

function Invoke-VsDevCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Write-Host $Description
    $developerCommand = "call `"$vsDevCmd`" -no_logo -arch=x86 -host_arch=x64 && $Command"
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
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

$cacheRoot = Join-Path $outRoot "dependency-cache"
$sourceRoot = Join-Path $outRoot "dependency-sources"
$buildRoot = Join-Path $outRoot "dependency-build"
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
        Write-Host "Downloading $($archive.Name)..."
        Invoke-WebRequest -Uri $archive.Uri -OutFile $archivePath
    }

    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($actualHash -ne $archive.Sha256) {
        throw "Checksum verification failed for '$($archive.Name)'. Expected $($archive.Sha256), received $actualHash."
    }

    Write-Host "Extracting $($archive.Name)..."
    & tar -xf $archivePath -C $sourceRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Extraction failed for '$($archive.Name)'."
    }
}

$opensslSource = Join-Path $sourceRoot "openssl-$($versions.openssl)"
$libssh2Source = Join-Path $sourceRoot "libssh2-$($versions.libssh2)"
$curlSource = Join-Path $sourceRoot "curl-$($versions.curl)"

$opensslCommand = "cd /d `"$opensslSource`" && perl Configure VC-WIN32 no-shared no-module no-tests no-apps no-docs no-asm --prefix=`"$InstallRoot`" --openssldir=`"$InstallRoot\ssl`" --libdir=lib && nmake /NOLOGO && nmake /NOLOGO install_sw"
Invoke-VsDevCommand -Command $opensslCommand -Description "Building OpenSSL $($versions.openssl) for Win32"

$libssh2Build = Join-Path $buildRoot "libssh2"
Invoke-CMake -Description "Configuring libssh2 $($versions.libssh2) for Win32" -Arguments @(
    "-S", $libssh2Source,
    "-B", $libssh2Build,
    "-G", "Visual Studio 17 2022",
    "-A", "Win32",
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
Invoke-CMake -Description "Configuring curl $($versions.curl) for Win32" -Arguments @(
    "-S", $curlSource,
    "-B", $curlBuild,
    "-G", "Visual Studio 17 2022",
    "-A", "Win32",
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
Write-Host "PDW Win32 dependencies were installed successfully to '$InstallRoot'."
