[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$failures = [System.Collections.Generic.List[string]]::new()
. (Join-Path $repoRoot "scripts\release-provenance.ps1")

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

function Require-NoMatch([string]$Text, [string]$Pattern, [string]$Description) {
    if ($Text -match $Pattern) {
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
Require-Match $manifest 'supportedOS Id="\{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a\}"' "PDW.manifest does not declare the Windows 10/11 compatibility ID."
if ([regex]::Matches($manifest, '<supportedOS\s+Id=').Count -ne 1) {
    Add-Failure "PDW.manifest must declare only the supported Windows 10/11 compatibility ID."
}

$workflow = Read-RepoFile ".github\workflows\build.yml"
Require-Match $workflow ([regex]::Escape("$packageBase-")) "GitHub artifact identity does not match $packageBase."
Require-NoMatch $workflow ('out\\build-[^\r\n]+\\Release\\' + [regex]::Escape($executableName)) "GitHub must not upload a bare executable without its app-local runtime and provenance marker."
Require-Match $workflow 'architecture: x64' "GitHub workflow does not contain an x64 target."
Require-Match $workflow 'architecture: x86' "GitHub workflow does not contain a Win32/x86 target."
Require-Match $workflow 'runs-on:\s*windows-2025-vs2026' "GitHub workflow does not pin the maintained Visual Studio 2026 runner."
Require-Match $workflow ([regex]::Escape('-VisualStudioMajor 18')) "GitHub workflow does not select Visual Studio 2026 explicitly."
Require-Match $workflow ([regex]::Escape('-T v145')) "GitHub workflow does not select the MSVC v145 toolset explicitly."
Require-Match $workflow ([regex]::Escape('.\scripts\audit-release.ps1')) "GitHub workflow does not run the release audit."
Require-Match $workflow ([regex]::Escape('.\scripts\package-release.ps1')) "GitHub workflow does not build both deterministic portable packages."
Require-Match $workflow ([regex]::Escape('out\build-${{ matrix.architecture }}')) "GitHub workflow must keep build output under ignored out/ so clean-tree packaging can run."
Require-Match $workflow 'PDWWasapiDeviceSmoke' "GitHub workflow does not compile-check the optional WASAPI device smoke target."
Require-Match $workflow 'PDWWinmmDeviceSmoke' "GitHub workflow does not compile-check the optional WinMM device smoke target."
Require-Match $workflow ([regex]::Escape('-OutputRoot "out\packages"')) "GitHub workflow does not place portable packages in its deterministic output root."
Require-Match $workflow ([regex]::Escape('.\scripts\build-installer.ps1')) "GitHub workflow does not build the guided installer."
Require-Match $workflow ([regex]::Escape('.\tests\installer_smoke.ps1')) "GitHub workflow does not test install, settings co-location, upgrade and uninstall."
Require-Match $workflow ([regex]::Escape("$packageBase-Setup.exe")) "GitHub workflow installer name does not match $packageBase."
Require-Match $workflow ([regex]::Escape("$packageBase-Portable-")) "GitHub workflow does not upload architecture-labelled portable packages."

$readme = Read-RepoFile "Readme"
$badgeLabel = [uri]::EscapeDataString($display.Substring(4))
Require-Match $readme ([regex]::Escape("version-$badgeLabel")) "Readme version badge does not match $display."
Require-Match $readme ([regex]::Escape("| **$display** |")) "Readme current release row does not match $display."
Require-Match $readme 'https://github\.com/ufo8mycow14/PDW(?:\)|/issues\)|/releases\))' "Readme does not identify the maintained fork and its current project links."

$resources = Read-RepoFile "Rsrc.rc"
Require-NoMatch $resources 'Native Win32 decoder' "Architecture-neutral VersionInfo must not label the x64 executable as Win32."

$changelog = Read-RepoFile "CHANGELOG.md"
$firstHeading = [regex]::Match($changelog, '(?m)^## ([^\r\n]+)$')
if (-not $firstHeading.Success -or $firstHeading.Groups[1].Value -ne $display) {
    Add-Failure "The first changelog release must be $display."
}

foreach ($required in @(
    "AGENTS.md", "SECURITY.md", "docs\PROJECT_RULES.md",
    "docs\DEPENDENCY_SECURITY.md", "docs\REPOSITORY_AUDIT.md",
    "docs\INSTALLATION.md", "installer\PDW.iss", "installer\INSTALL_NOTICE.txt",
    "packaging\PDW.INI", "packaging\PDW-Adelaide-FLEX.INI",
    "docs\SDRSHARP_VBCABLE_PROFILE.md",
    "scripts\stage-installer-input.ps1", "scripts\build-installer.ps1",
    "scripts\audit-installer.ps1",
    "tests\installer_smoke.ps1", "tests\source_archive_smoke.ps1",
    "scripts\validate-source-archive.cmake",
    "scripts\write-build-provenance.cmake",
    "scripts\invalidate-build-provenance.cmake"
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
Require-Match $installer '(?m)^MinVersion=10\.0\.10586\s*$' "Installer does not enforce the Windows 10 build 10586 / Server 2016 support floor."
Require-Match $installer 'RequestedProfile' "Installer does not validate a clean-install profile choice."
Require-Match $installer 'adelaide-flex' "Installer does not offer the Adelaide FLEX profile."
Require-Match $installer 'PDW-Adelaide-FLEX\.INI' "Installer does not stage the Adelaide FLEX profile."
Require-NoMatch $installer '(?m)^Source: .*\\filters\.ini"' "Installer must not deploy a fresh retired filters.ini."

$standardProfile = Read-RepoFile "packaging\PDW.INI"
$adelaideProfile = Read-RepoFile "packaging\PDW-Adelaide-FLEX.INI"
Require-Match $standardProfile '(?m)^AudioEnabled=1$' "Standard profile no longer preserves established local-audio startup behavior."
Require-Match $standardProfile '(?m)^PresetId=$' "Standard profile silently selects a named input profile."
Require-Match $standardProfile '(?m)^IdentityInvalid=0$' "Standard profile lacks a valid durable endpoint-identity state."
Require-Match $adelaideProfile '(?m)^PresetId=sdrsharp-vbcable-adelaide-flex-v1$' "Adelaide FLEX profile ID is missing."
Require-Match $adelaideProfile '(?m)^DeviceEndpointId=$' "Packaged Adelaide profile must not contain a machine-specific endpoint ID."
Require-Match $adelaideProfile '(?m)^DeviceFriendlyName=CABLE Output \(VB-Audio Virtual Cable\)$' "Adelaide FLEX profile capture name is missing."
Require-Match $adelaideProfile '(?m)^IdentityInvalid=0$' "Adelaide FLEX profile lacks a valid durable endpoint-identity state."

$packageScript = Read-RepoFile "scripts\package-release.ps1"
$stageScript = Read-RepoFile "scripts\stage-installer-input.ps1"
$buildInstallerScript = Read-RepoFile "scripts\build-installer.ps1"
$dependencyScript = Read-RepoFile "scripts\build-dependencies.ps1"
$generatorScript = Read-RepoFile "scripts\resolve-cmake-generator.ps1"
$installerSmoke = Read-RepoFile "tests\installer_smoke.ps1"
$sourceArchiveSmoke = Read-RepoFile "tests\source_archive_smoke.ps1"
$releaseProvenance = Read-RepoFile "scripts\release-provenance.ps1"
$releaseProvenanceSmoke = Read-RepoFile "tests\release_provenance_smoke.ps1"
$installerBuildFailSmoke = Read-RepoFile "tests\installer_build_fail_closed_smoke.ps1"
$cmakeScript = Read-RepoFile "CMakeLists.txt"
$installationGuide = Read-RepoFile "docs\INSTALLATION.md"
$dependencyGuide = Read-RepoFile "docs\DEPENDENCY_SECURITY.md"
$notices = Read-RepoFile "THIRD_PARTY_NOTICES.md"
Require-Match $dependencyScript 'ValidateSet\(17,\s*18\)' "Dependency builds must restrict supported Visual Studio majors to 2022 and 2026."
Require-Match $generatorScript 'ValidateSet\(17,\s*18\)' "Generator resolution must reject unknown future Visual Studio/toolset mappings."
Require-Match $dependencyScript 'cmakeToolset\s*=\s*\$cmakeToolset' "Dependency locks do not record the explicit CMake toolset."
Require-Match $dependencyScript 'cmakeGenerator\s*=\s*\$cmakeGenerator' "Dependency locks do not record the CMake generator."
Require-Match $dependencyScript 'cmakeVersion\s*=\s*\$cmakeVersion' "Dependency locks do not record the CMake version."
Require-Match $dependencyScript 'resolverSha256\s*=\s*\$resolverSha256' "Dependency locks do not bind the generator resolver recipe."
Require-Match $packageScript 'PDW-Adelaide-FLEX\.INI' "Portable packaging does not include the optional profile."
Require-Match $packageScript 'SOURCE_COMMIT\.txt' "Portable packaging does not record its exact source commit."
Require-Match $packageScript 'LastWriteTimeUtc\s+-lt\s+\$sourceCommitTime' "Portable packaging does not reject an executable older than the source commit."
Require-Match $cmakeScript 'include\(InstallRequiredSystemLibraries\)' "CMake does not use InstallRequiredSystemLibraries for the app-local VC runtime."
Require-Match $cmakeScript 'set\(CMAKE_INSTALL_UCRT_LIBRARIES FALSE\)' "CMake does not explicitly exclude app-local UCRT distribution."
Require-Match $cmakeScript 'set\(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE\)' "CMake does not retain explicit control of runtime copying."
Require-Match $cmakeScript 'add_custom_command\(TARGET PDW POST_BUILD' "CMake does not copy release support files beside PDW after build."
Require-Match $cmakeScript 'PDW_BUILD_COMMIT\.txt' "CMake does not emit the build provenance marker."
Require-Match $cmakeScript 'validate-source-archive\.cmake' "CMake does not validate provenance when building packaged source without .git."
Require-Match $cmakeScript 'PDW_CONFIGURE_MARKER_STATE "pending"' "A clean configure can incorrectly bless an executable that has not been linked."
Require-Match $cmakeScript 'add_custom_command\(TARGET PDW PRE_LINK' "CMake does not invalidate stale target provenance before linking."
Require-NoMatch $cmakeScript '(?im)(?:^|[;"\s])/MTd?(?:[;"\s]|$)' "PDW must remain dynamically linked with /MD rather than /MT."
Require-Match $packageScript 'Assert-BuildCommitMarker' "Portable packaging does not bind the executable to exact clean HEAD provenance."
Require-Match $packageScript 'PDW_SOURCE_PROVENANCE\.txt' "Portable source does not carry validated release-source provenance."
Require-Match $packageScript 'PDW_SOURCE_SHA256SUMS\.txt' "Portable source does not carry its own SHA-256 file manifest."
Require-Match $workflow 'source_archive_smoke\.ps1' "CI does not rebuild and tamper-test the extracted portable Source tree."
Require-Match $sourceArchiveSmoke 'TamperRejected' "Extracted-source smoke does not reject a changed manifested file."
Require-Match $sourceArchiveSmoke 'MissingFileRejected' "Extracted-source smoke does not reject a missing manifested file."
Require-Match $sourceArchiveSmoke 'ExtraFileRejected' "Extracted-source smoke does not reject an unlisted source file."
Require-Match $sourceArchiveSmoke 'StaleMarkerFailClosed' "Extracted-source smoke does not regress a failed link-time provenance promotion."
Require-Match $stageScript 'Assert-BuildCommitMarker' "Installer staging does not bind the executable to exact clean HEAD provenance."
Require-Match $buildInstallerScript 'Assert-StagedBuildCommit' "Setup compilation does not verify both staged provenance markers."
foreach ($releaseScript in @(
    @{ Text = $packageScript; Label = "Portable packaging" },
    @{ Text = $stageScript; Label = "Installer staging" }
)) {
    Require-Match $releaseScript.Text 'New-PdwGitCommitSnapshot' "$($releaseScript.Label) does not take tracked inputs from one immutable commit snapshot."
    Require-Match $releaseScript.Text 'Copy-PdwFileHashStable' "$($releaseScript.Label) does not reject build artifacts changed while copied."
    Require-Match $releaseScript.Text 'Assert-PdwExactCleanGitHead' "$($releaseScript.Label) does not recheck exact clean HEAD after staging."
}
Require-Match $stageScript 'Write-PdwInstallerInputManifest' "Installer staging does not bind every staged byte to an exact SHA-256 manifest."
Require-Match $buildInstallerScript 'Assert-PdwInstallerInputManifest' "Setup compilation does not validate staged SHA-256 manifests."
Require-Match $buildInstallerScript 'New-PdwGitCommitSnapshot' "Setup compilation does not use immutable tracked installer inputs."
Require-Match $buildInstallerScript 'installerInputSnapshot' "Setup compilation does not read both architectures from validated immutable input snapshots."
Require-Match $buildInstallerScript 'compilerOutputRoot' "Setup compilation does not isolate unvalidated compiler output from public release filenames."
Require-Match $buildInstallerScript 'Publish the executable last' "Setup publication does not keep the public executable absent through all release gates."
Require-Match $installer 'PDW_INSTALLER_INPUT_SHA256SUMS\.txt' "Installer does not explicitly exclude its staging-only SHA-256 manifest."
Require-Match $workflow 'release_provenance_smoke\.ps1' "CI does not regress post-preflight mutation and installer-input tampering."
Require-Match $workflow 'installer_build_fail_closed_smoke\.ps1' "CI does not regress compiler-success/postvalidation-failure cleanup."
Require-Match $releaseProvenanceSmoke 'PostPreflightMutationRejected' "Release provenance smoke does not inject and reject a post-preflight mutation."
Require-Match $releaseProvenanceSmoke 'ManifestTamperRejected' "Release provenance smoke does not reject modified staged installer input."
Require-Match $releaseProvenanceSmoke 'ManifestExtraRejected' "Release provenance smoke does not reject unlisted staged installer input."
Require-Match $releaseProvenanceSmoke 'DirectoryManifestTamperRejected' "Release provenance smoke does not reject a modified portable-package file."
Require-Match $releaseProvenanceSmoke 'ExpandedZipExtraRejected' "Release provenance smoke does not reject an unlisted file from an expanded portable ZIP."
Require-Match $releaseProvenanceSmoke 'DirectoryDestinationOccupancyRejected' "Release provenance smoke does not reject an occupied directory publication destination."
Require-Match $releaseProvenanceSmoke 'ZipDestinationOccupancyRejected' "Release provenance smoke does not reject an occupied ZIP publication destination."
Require-Match $releaseProvenanceSmoke 'pre-adelaide-flex-20260811\.bak\.2' "Release provenance smoke does not reject a collision-suffixed profile backup."
Require-Match $releaseProvenanceSmoke 'operator-swap-2' "Release provenance smoke does not reject a hyphen-suffixed operator-swap file."
Require-Match $installerBuildFailSmoke 'PostcompileMutationRejected' "Setup fail-closed smoke does not inject and reject a postcompile input mutation."
Require-Match $installerBuildFailSmoke 'PublicSetupAbsent' "Setup fail-closed smoke does not prove failed output remains unpublished."
Require-Match $installerBuildFailSmoke 'PostPolicyMutationRejected' "Setup fail-closed smoke does not reject mutation after Authenticode policy evaluation."
Require-Match $installerBuildFailSmoke 'DestinationOccupancyRejected' "Setup fail-closed smoke does not preserve an occupied final Setup destination."
Require-Match $releaseProvenance 'PDW-INSTALLER-INPUT-MANIFEST v1' "Installer-input manifest format is not versioned."
Require-Match $installerSmoke 'PDW_BUILD_COMMIT\.txt' "Installer smoke does not verify installed source provenance."
Require-Match $workflow 'out\\installer-input\\Win32' "Installer workflow inputs must remain under ignored out/ while exact-clean-HEAD checks run."
Require-Match $workflow 'DependencyEvidence' "Workflow does not label non-runnable dependency evidence explicitly."
Require-Match $stageScript 'PDW-Adelaide-FLEX\.INI' "Installer staging does not include the optional profile."
Require-NoMatch $stageScript 'packaging\\filters\.ini' "Installer staging must not restore fresh filters.ini deployment."
Require-Match $packageScript '(?s)\$forbidden\s*=\s*Get-ChildItem.*?-Recurse.*?\.Name\s+-ieq\s+''filters\.ini''' "Portable packaging does not recursively reject a fresh filters.ini."
Require-Match $stageScript '(?s)\$forbiddenArtifacts\s*=\s*Get-ChildItem.*?-Recurse.*?\.Name\s+-ieq\s+"filters\.ini"' "Installer staging does not recursively reject a fresh filters.ini."
Require-Match $releaseProvenance 'function Test-PdwPrivateTransactionArtifactName' "Release provenance does not define the shared private transaction-artifact predicate."
Require-Match $packageScript 'Test-PdwPrivateTransactionArtifactName' "Portable packaging does not use the shared local-audio transaction-artifact predicate."
Require-Match $stageScript 'Test-PdwPrivateTransactionArtifactName' "Installer staging does not use the shared local-audio transaction-artifact predicate."
Require-Match $releaseProvenance '\.pre-adelaide-flex-\.\*\$' "Private backup collision suffixes are not rejected."
Require-Match $releaseProvenance '\.operator-swap\.\*\$' "Private operator-swap suffixes are not rejected."
Require-Match $packageScript 'transaction artifact contains a non-empty sensitive field' "Portable packaging does not treat INI-like transaction files as sensitive."
Require-Match $stageScript 'transaction artifact contains a non-empty sensitive field' "Installer staging does not treat INI-like transaction files as sensitive."
Require-Match $releaseProvenance 'Get-PdwSensitiveIniFieldName' "Release provenance does not define the shared sensitive INI-field predicate."
Require-Match $releaseProvenance 'DeviceEndpointId' "Release provenance does not reject a persisted machine endpoint identity."
Require-Match $packageScript 'Get-PdwSensitiveIniFieldName' "Portable packaging does not use the shared sensitive INI-field predicate."
Require-Match $stageScript 'Get-PdwSensitiveIniFieldName' "Installer staging does not use the shared sensitive INI-field predicate."
Require-Match $releaseProvenanceSmoke 'DeviceEndpointId=utf8-hex' "Release provenance smoke does not reject a synthetic persisted endpoint identity."
Require-Match $releaseProvenance 'Assert-PdwDirectoryHashManifest' "Release provenance does not define portable-directory hash validation."
Require-Match $releaseProvenance 'Move-PdwDirectoryNoReplace' "Release provenance does not define atomic no-replace directory publication."
Require-Match $releaseProvenance 'Move-PdwFileNoReplace' "Release provenance does not define atomic no-replace file publication."
Require-Match $packageScript 'Assert-PdwDirectoryHashManifest' "Portable packaging does not validate its folder and expanded ZIP against SHA256SUMS."
Require-Match $packageScript 'atomic no-replace directory' "Portable packaging does not publish the folder and ZIP as one atomic set."
Require-Match $stageScript 'Move-PdwDirectoryNoReplace' "Installer input staging does not use atomic no-replace publication."
Require-Match $buildInstallerScript 'Move-PdwFileNoReplace' "Setup publication does not use atomic no-replace file publication."
Require-NoMatch $packageScript '(?i)LegacyAssetsRoot|COMPRT\.VXD|Comprt2\.vxd|xp_driver\.zip' "Portable packaging must not copy ambient untracked legacy kernel-driver assets."
Require-NoMatch $stageScript '(?i)LegacyAssetsRoot|COMPRT\.VXD|Comprt2\.vxd|xp_driver\.zip' "Installer staging must not copy ambient untracked legacy kernel-driver assets."
foreach ($runtimeName in @(
    'concrt140.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
    'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll',
    'vcruntime140.dll', 'vcruntime140_1.dll'
)) {
    $escapedRuntime = [regex]::Escape($runtimeName)
    Require-Match $cmakeScript $escapedRuntime "CMake app-local runtime allowlist is missing $runtimeName."
    Require-Match $packageScript $escapedRuntime "Portable runtime allowlist is missing $runtimeName."
    Require-Match $stageScript $escapedRuntime "Installer-stage runtime allowlist is missing $runtimeName."
    Require-Match $installer ('(?m)^Type: files; Name: "\{app\}\\' + $escapedRuntime + '"$') "Installer does not remove stale $runtimeName before an architecture switch."
    Require-Match $installerSmoke $escapedRuntime "Installer smoke runtime allowlist is missing $runtimeName."
}
Require-Match $packageScript '\^14\\\.5\[0-9\]\\\.' "Portable packaging does not enforce the Microsoft VC145 runtime version family."
Require-Match $stageScript '\^14\\\.5\[0-9\]\\\.' "Installer staging does not enforce the Microsoft VC145 runtime version family."
Require-Match $installerSmoke '\^14\\\.5\[0-9\]\\\.' "Installer smoke does not verify the Microsoft VC145 runtime version family."
Require-NoMatch $cmakeScript 'set\(CMAKE_INSTALL_UCRT_LIBRARIES TRUE\)' "CMake must not select app-local UCRT libraries."
Require-NoMatch $installer '(?im)^Source: .*?(?:vc_redist|ucrtbase\.dll|api-ms-win-crt-)' "Installer must not source a runtime installer or UCRT DLL."
Require-NoMatch ($packageScript + $stageScript) '(?im)(?:Copy-Item|Source\s*=).*?(?:vc_redist|ucrtbase\.dll|api-ms-win-crt-)' "Release staging must not copy a runtime installer or UCRT DLL."
Require-Match $installationGuide 'app-local Microsoft Visual C\+\+ runtime' "Installation guide does not explain the app-local VC runtime boundary."
Require-Match $dependencyGuide 'Microsoft Visual C\+\+ runtime DLLs' "Dependency review does not record the VC runtime distribution decision."
Require-Match $notices 'Microsoft Visual C\+\+ runtime' "Third-party notices do not record Microsoft runtime redistribution."
Require-Match $installerSmoke 'adelaide-flex' "Installer smoke does not exercise the Adelaide profile."
Require-Match $installerSmoke 'PDW v5\.4 2026 Release\.exe' "Installer smoke does not verify v5.4 predecessor cleanup."
Require-Match $installerSmoke 'pdw-history\.sqlite3' "Installer smoke does not preserve the Capcode Directory database."
Require-Match $installerSmoke 'legacyFilters' "Installer smoke does not preserve a pre-existing legacy filters.ini."
Require-Match $installerSmoke 'UseInstalledArchitecture' "Installer smoke does not verify the saved architecture-marker default."
Require-Match $installerSmoke 'DefaultUpgradeArchitecture' "Installer smoke does not report saved architecture-marker coverage."
Require-Match $installerSmoke 'Test-CrossArchitectureReceiverBackup' "Installer smoke does not exercise cross-architecture receiver backup."
Require-Match $installerSmoke 'pre-x64-architecture.*\.bak' "Installer smoke does not verify preservation of an x64 receiver during architecture change."
Require-Match $installerSmoke 'pre-win32-architecture.*\.bak' "Installer smoke does not verify preservation of a Win32 receiver during architecture change."

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
    "GFX/pdwlogo3.bmp", "resrc1.h", "COMPRT.VXD", "Comprt2.vxd",
    "xp_driver.zip"
)
$returnedObsolete = @($tracked | Where-Object { $obsolete -contains $_ })
if ($returnedObsolete.Count -gt 0) {
    Add-Failure ("Obsolete repository-audit files have returned: " + ($returnedObsolete -join ', '))
}
$obsoleteKernelNames = @("COMPRT.VXD", "Comprt2.vxd", "xp_driver.zip")
$returnedKernelAssets = @($tracked | Where-Object {
    $obsoleteKernelNames -contains [System.IO.Path]::GetFileName($_)
})
if ($returnedKernelAssets.Count -gt 0) {
    Add-Failure ("Obsolete untracked-kernel-driver assets have returned: " +
        ($returnedKernelAssets -join ', '))
}
$trackedFilters = @($tracked | Where-Object {
    [System.IO.Path]::GetFileName($_) -ieq "filters.ini"
})
if ($trackedFilters.Count -gt 0) {
    Add-Failure ("Fresh-package filters.ini has returned; the Capcode Directory owns filter persistence: " +
        ($trackedFilters -join ', '))
}

$trackedTransactionArtifacts = @($tracked | Where-Object {
    $name = [System.IO.Path]::GetFileName($_)
    Test-PdwPrivateTransactionArtifactName $name
})
if ($trackedTransactionArtifacts.Count -gt 0) {
    Add-Failure ("Private local-audio transaction artifacts are tracked: " +
        ($trackedTransactionArtifacts -join ', '))
}

$dependencyScript = Read-RepoFile "scripts\build-dependencies.ps1"
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
