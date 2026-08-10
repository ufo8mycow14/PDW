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
    return Get-Content -LiteralPath $path -Raw
}

function Require-Match([string]$Text, [string]$Pattern, [string]$Description) {
    if ($Text -notmatch $Pattern) {
        Add-Failure $Description
    }
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

$display = "PDW v$version Beta"
$resourceVersion = "$version.0"
Require-Match $versionHeader ('(?m)^#define PDW_VERSION_RESOURCE_STRING "' + [regex]::Escape($resourceVersion) + '"$') "Resource version string does not match $resourceVersion."
Require-Match $versionHeader ('(?m)^#define PDW_VERSION_STRING "' + [regex]::Escape("$version Beta") + '"$') "Product version string does not match $version Beta."
Require-Match $versionHeader ('(?m)^#define PDW_DISPLAY_VERSION "' + [regex]::Escape($display) + '"$') "Display version does not match $display."
Require-Match $versionHeader ('(?m)^#define PDW_EXECUTABLE_NAME "' + [regex]::Escape("$display.exe") + '"$') "Executable name does not match $display.exe."

$manifest = Read-RepoFile "PDW.manifest"
Require-Match $manifest ('assemblyIdentity version="' + [regex]::Escape($resourceVersion) + '"') "PDW.manifest does not match $resourceVersion."

$workflow = Read-RepoFile ".github\workflows\build.yml"
Require-Match $workflow ([regex]::Escape("PDW-v$version-Beta-")) "GitHub artifact version does not match $version."
Require-Match $workflow ([regex]::Escape("PDW v$version Beta.exe")) "GitHub executable path does not match $version."
Require-Match $workflow 'architecture: x64' "GitHub workflow does not contain an x64 target."
Require-Match $workflow 'architecture: x86' "GitHub workflow does not contain a Win32/x86 target."
Require-Match $workflow ([regex]::Escape('.\scripts\audit-release.ps1')) "GitHub workflow does not run the release audit."

$readme = Read-RepoFile "Readme"
Require-Match $readme ([regex]::Escape("version-$version%20Beta")) "Readme version badge does not match $version Beta."
Require-Match $readme ([regex]::Escape("| **$version Beta** |")) "Readme current release row does not match $version Beta."

$changelog = Read-RepoFile "CHANGELOG.md"
$firstHeading = [regex]::Match($changelog, '(?m)^## ([^\r\n]+)$')
if (-not $firstHeading.Success -or $firstHeading.Groups[1].Value -ne "$version Beta") {
    Add-Failure "The first changelog release must be $version Beta."
}

foreach ($required in @("AGENTS.md", "SECURITY.md", "docs\PROJECT_RULES.md", "docs\DEPENDENCY_SECURITY.md", "docs\REPOSITORY_AUDIT.md")) {
    [void](Read-RepoFile $required)
}

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
