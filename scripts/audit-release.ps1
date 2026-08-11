[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure([string]$Message) {
    $failures.Add($Message)
}

function Read-RepoFile([string]$RelativePath) {
    $path = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Add-Failure "Required release file is missing: $RelativePath"
        return ""
    }
    # GitHub Actions uses PowerShell 7, while the development machine can use
    # Windows PowerShell 5.1. Normalize line endings so anchored release
    # metadata checks behave identically for CRLF and LF checkouts.
    return (Get-Content -LiteralPath $path -Raw).Replace("`r`n", "`n").Replace("`r", "`n")
}

function Require-Match([string]$Text, [string]$Pattern, [string]$Description) {
    if ($Text -notmatch $Pattern) {
        Add-Failure $Description
    }
}

function Read-HeaderStringMacro([string]$Text, [string]$Name) {
    $match = [regex]::Match($Text,
        '(?m)^#define ' + [regex]::Escape($Name) + ' "([^"]+)"$')
    if (-not $match.Success) {
        Add-Failure "Headers/version.h does not contain $Name."
        return ""
    }
    return $match.Groups[1].Value
}

$versionHeader = Read-RepoFile "Headers\version.h"
$majorMatch = [regex]::Match($versionHeader, '(?m)^#define PDW_VERSION_MAJOR ([0-9]+)$')
$minorMatch = [regex]::Match($versionHeader, '(?m)^#define PDW_VERSION_MINOR ([0-9]+)$')
$patchMatch = [regex]::Match($versionHeader, '(?m)^#define PDW_VERSION_PATCH ([0-9]+)$')
if (-not $majorMatch.Success -or -not $minorMatch.Success -or -not $patchMatch.Success) {
    Add-Failure "Headers/version.h does not contain the canonical numeric version macros."
    $version = "0.0.0"
}
else {
    $version = "$($majorMatch.Groups[1].Value).$($minorMatch.Groups[1].Value).$($patchMatch.Groups[1].Value)"
}

$display = Read-HeaderStringMacro $versionHeader "PDW_DISPLAY_VERSION"
$executableName = Read-HeaderStringMacro $versionHeader "PDW_EXECUTABLE_NAME"
$productVersion = Read-HeaderStringMacro $versionHeader "PDW_VERSION_STRING"
$packageBase = Read-HeaderStringMacro $versionHeader "PDW_PACKAGE_BASENAME"
$resourceVersion = "$version.0"
Require-Match $versionHeader ('(?m)^#define PDW_VERSION_RESOURCE_STRING "' + [regex]::Escape($resourceVersion) + '"$') "Resource version string does not match $resourceVersion."
Require-Match $versionHeader ('(?m)^#define PDW_VERSION_RESOURCE ' +
    [regex]::Escape("$($majorMatch.Groups[1].Value),$($minorMatch.Groups[1].Value),$($patchMatch.Groups[1].Value),0") + '$') "Numeric resource version does not match $resourceVersion."
if ($productVersion -notmatch ('^' + [regex]::Escape($version) + ' ')) {
    Add-Failure "Product version string does not begin with canonical version $version."
}
if ($executableName -ne "$display.exe") {
    Add-Failure "Executable name does not match $display.exe."
}
if ($packageBase -notmatch '^PDW-') {
    Add-Failure "Package basename must begin with PDW-."
}

$manifest = Read-RepoFile "PDW.manifest"
Require-Match $manifest ('assemblyIdentity version="' + [regex]::Escape($resourceVersion) + '"') "PDW.manifest does not match $resourceVersion."

$workflow = Read-RepoFile ".github\workflows\build.yml"
Require-Match $workflow ([regex]::Escape("$packageBase-")) "GitHub artifact identity does not match $packageBase."
Require-Match $workflow ([regex]::Escape($executableName)) "GitHub executable path does not match $executableName."
Require-Match $workflow 'architecture: x64' "GitHub workflow does not contain an x64 target."
Require-Match $workflow 'architecture: x86' "GitHub workflow does not contain a Win32/x86 target."
Require-Match $workflow ([regex]::Escape('.\scripts\audit-release.ps1')) "GitHub workflow does not run the release audit."
Require-Match $workflow ([regex]::Escape('.\scripts\build-installer.ps1')) "GitHub workflow does not build the guided installer."
Require-Match $workflow ([regex]::Escape('.\tests\installer_smoke.ps1')) "GitHub workflow does not test install, settings co-location, upgrade and uninstall."
Require-Match $workflow ([regex]::Escape("$packageBase-Setup.exe")) "GitHub workflow installer name does not match $packageBase."

$readme = Read-RepoFile "Readme"
$badgeLabel = [uri]::EscapeDataString($display.Substring(4))
Require-Match $readme ([regex]::Escape("version-$badgeLabel")) "Readme version badge does not match $display."
Require-Match $readme ([regex]::Escape("| **$display** |")) "Readme current release row does not match $display."

$changelog = Read-RepoFile "CHANGELOG.md"
$firstHeading = [regex]::Match($changelog, '(?m)^## ([^\r\n]+)$')
if (-not $firstHeading.Success -or $firstHeading.Groups[1].Value -ne $display) {
    Add-Failure "The first changelog release must be $display."
}

foreach ($required in @(
    "AGENTS.md", "SECURITY.md", "docs\PROJECT_RULES.md",
    "docs\DEPENDENCY_SECURITY.md", "docs\REPOSITORY_AUDIT.md",
    "docs\INSTALLATION.md", "installer\PDW.iss", "installer\INSTALL_NOTICE.txt",
    "scripts\stage-installer-input.ps1", "scripts\build-installer.ps1",
    "scripts\audit-installer.ps1",
    "tests\installer_smoke.ps1"
)) {
    [void](Read-RepoFile $required)
}

$installer = Read-RepoFile "installer\PDW.iss"
Require-Match $installer ('(?m)^#define AppName "' + [regex]::Escape($display) + '"$') "Installer AppName does not match $display."
Require-Match $installer ('(?m)^#define AppVersion "' + [regex]::Escape($version) + '"$') "Installer AppVersion does not match $version."
Require-Match $installer ('(?m)^#define AppExeName "' + [regex]::Escape($executableName) + '"$') "Installer executable does not match $executableName."
Require-Match $installer ('(?m)^#define SetupBaseName "' + [regex]::Escape("$packageBase-Setup") + '"$') "Installer filename does not match $packageBase."
Require-Match $installer ('(?m)^VersionInfoVersion=' + [regex]::Escape($resourceVersion) + '$') "Installer file version does not match $resourceVersion."
Require-Match $installer ('(?m)^VersionInfoProductVersion=' + [regex]::Escape($resourceVersion) + '$') "Installer product version does not match $resourceVersion."
Require-Match $installer ('(?m)^VersionInfoDescription=' + [regex]::Escape("$display Setup") + '$') "Installer description does not match $display."
Require-Match $installer ('(?m)^VersionInfoProductName=' + [regex]::Escape($display) + '$') "Installer product name does not match $display."

$tracked = @(& git -C $repoRoot ls-files)
if ($LASTEXITCODE -ne 0) {
    Add-Failure "git ls-files failed; tracked-file hygiene could not be checked."
}
$visualBasic = @($tracked | Where-Object { $_ -match '(?i)\.(vb|vbp|bas|frm|cls|vbproj)$' })
if ($visualBasic.Count -gt 0) {
    Add-Failure ("Visual Basic files require explicit runtime, security, licensing, and dual-architecture review: " + ($visualBasic -join ', '))
}
$obsolete = @(
    "PDW.dep", "PDW.dsp", "PDW.dsw", "PDW.mak", "PDW.opt", "PDW.plg",
    "pdw_vs2017.sln", "pdw_vs2017.vcxproj", "pdw_vs2017.vcxproj.filters",
    "pdw_vs2017.vcxproj.user", "Rsrc.aps", "Rsrc.clw", "pdw3.1-full.zip",
    "Headers/html.h", "utils/globals.h", "utils/OSTYPE.C", "GFX/close.bmp",
    "GFX/pdwlogo3.bmp", "resrc1.h"
)
$returnedObsolete = @($tracked | Where-Object { $obsolete -contains $_ })
if ($returnedObsolete.Count -gt 0) {
    Add-Failure ("Obsolete repository-audit files have returned: " + ($returnedObsolete -join ', '))
}

$dependencyScript = Read-RepoFile "scripts\build-dependencies.ps1"
$notices = Read-RepoFile "THIRD_PARTY_NOTICES.md"
foreach ($dependency in @(
    @{ Name = "OpenSSL"; Key = "openssl" },
    @{ Name = "curl/libcurl"; Key = "curl" },
    @{ Name = "libssh2"; Key = "libssh2" }
)) {
    $match = [regex]::Match($dependencyScript, '(?m)^\s*' + $dependency.Key + ' = "([0-9.]+)"\r?$')
    if (-not $match.Success) {
        Add-Failure "Could not read the pinned $($dependency.Name) version."
        continue
    }
    Require-Match $notices ('\| ' + [regex]::Escape($dependency.Name) + ' \| ' + [regex]::Escape($match.Groups[1].Value) + ' \|') "THIRD_PARTY_NOTICES.md does not match pinned $($dependency.Name) $($match.Groups[1].Value)."
}

$sqlite = Read-RepoFile "utils\sqlite_output.cpp"
Require-Match $sqlite 'IsSafeSqlIdentifier\(table\)' "SQLite table identifiers are not visibly validated."
Require-Match $sqlite 'sqlite3_prepare_v2' "SQLite event writes are not visibly prepared."
Require-Match $sqlite 'sqlite3_bind_text' "SQLite event text is not visibly bound."
$mysql = Read-RepoFile "utils\mysql_odbc_output.cpp"
Require-Match $mysql 'IsSafeSqlIdentifier\(table\)' "MySQL table identifiers are not visibly validated."
Require-Match $mysql 'SQLPrepareA' "MySQL event writes are not visibly prepared."
Require-Match $mysql 'SQLBindParameter' "MySQL event values are not visibly bound."

if ($failures.Count -gt 0) {
    Write-Error ("PDW release audit failed:`n - " + ($failures -join "`n - "))
    exit 1
}

Write-Host "PDW release audit passed for $display."
Write-Host "Targets: x64 and Win32/x86; no tracked Visual Basic or obsolete audited files."
Write-Host "Pinned dependency notices and prepared-statement SQL safeguards are aligned."
