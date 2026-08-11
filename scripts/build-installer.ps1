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
. (Join-Path $PSScriptRoot "release-provenance.ps1")
$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$win32Root = (Resolve-Path -LiteralPath $Win32ApplicationDirectory).Path
$x64Root = (Resolve-Path -LiteralPath $X64ApplicationDirectory).Path

function Assert-StagedBuildCommit([string]$Directory, [string]$ExpectedCommit) {
    $marker = Join-Path $Directory "PDW_BUILD_COMMIT.txt"
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw "Installer input provenance marker is missing: $marker"
    }
    $expectedText = "commit=$ExpectedCommit`nstate=clean`n"
    $actualText = (Get-Content -LiteralPath $marker -Raw).Replace("`r`n", "`n").Replace("`r", "`n")
    if ($actualText -cne $expectedText) {
        throw "Installer input does not identify exact clean source commit $ExpectedCommit`: $marker"
    }
}

$sourceCommit = Get-PdwExactCleanGitHead $sourceRoot "building Setup"
Assert-StagedBuildCommit $win32Root $sourceCommit
Assert-StagedBuildCommit $x64Root $sourceCommit
Assert-PdwInstallerInputManifest $win32Root "Win32" $sourceCommit
Assert-PdwInstallerInputManifest $x64Root "x64" $sourceCommit
$win32Manifest = Join-Path $win32Root $script:PdwInstallerInputManifestName
$x64Manifest = Join-Path $x64Root $script:PdwInstallerInputManifestName
$win32ManifestHash = (Get-FileHash -LiteralPath $win32Manifest -Algorithm SHA256).Hash
$x64ManifestHash = (Get-FileHash -LiteralPath $x64Manifest -Algorithm SHA256).Hash
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

if ([string]::IsNullOrWhiteSpace($SignToolName) -ne [string]::IsNullOrWhiteSpace($SignToolCommand)) {
    throw "SignToolName and SignToolCommand must be supplied together."
}

$installerSourceSnapshot = Join-Path $outputRoot ("pdw-installer-source-" +
    [guid]::NewGuid().ToString("N"))
$installerInputSnapshot = Join-Path $outputRoot ("pdw-installer-input-" +
    [guid]::NewGuid().ToString("N"))
$compilerOutputRoot = Join-Path $outputRoot ("pdw-installer-output-" +
    [guid]::NewGuid().ToString("N"))
$publishedSet = $false
$finalSet = $null
$publicationSet = $null
$setup = $null
$setupHashPath = $null
try {
    New-PdwGitCommitSnapshot $sourceRoot $sourceCommit $installerSourceSnapshot
    New-Item -ItemType Directory -Path $installerInputSnapshot | Out-Null
    New-Item -ItemType Directory -Path $compilerOutputRoot | Out-Null
    $compileWin32Root = Join-Path $installerInputSnapshot "Win32"
    $compileX64Root = Join-Path $installerInputSnapshot "x64"
    Copy-Item -LiteralPath $win32Root -Destination $compileWin32Root -Recurse
    Copy-Item -LiteralPath $x64Root -Destination $compileX64Root -Recurse
    Assert-PdwInstallerInputManifest $compileWin32Root "Win32" $sourceCommit
    Assert-PdwInstallerInputManifest $compileX64Root "x64" $sourceCommit
    if ((Get-FileHash -LiteralPath (Join-Path $compileWin32Root $script:PdwInstallerInputManifestName) -Algorithm SHA256).Hash -cne
            $win32ManifestHash -or
        (Get-FileHash -LiteralPath (Join-Path $compileX64Root $script:PdwInstallerInputManifestName) -Algorithm SHA256).Hash -cne
            $x64ManifestHash) {
        throw "Installer input changed while the immutable Setup compilation snapshot was copied."
    }

    $versionHeader = Get-Content -LiteralPath (Join-Path $installerSourceSnapshot "Headers\version.h") -Raw
    $packageMatch = [regex]::Match($versionHeader,
        '(?m)^#define PDW_PACKAGE_BASENAME "([^"]+)"\r?$')
    if (-not $packageMatch.Success) {
        throw "Unable to read PDW_PACKAGE_BASENAME from Headers\version.h."
    }
    $setupName = "$($packageMatch.Groups[1].Value)-Setup.exe"
    $setName = "$($packageMatch.Groups[1].Value)-Setup-package"
    $finalSet = Join-Path $outputRoot $setName
    $setup = Join-Path $finalSet $setupName
    $setupHashPath = "$setup.sha256"
    if ((Test-Path -LiteralPath $finalSet) -or
        (Test-Path -LiteralPath (Join-Path $outputRoot $setupName)) -or
        (Test-Path -LiteralPath (Join-Path $outputRoot "$setupName.sha256"))) {
        throw "Installer output or checksum already exists; refusing to overwrite: $setup"
    }
    $temporarySetup = Join-Path $compilerOutputRoot $setupName
    $temporarySetupHash = "$temporarySetup.sha256"

    $arguments = @(
        "/DWin32Application=$compileWin32Root",
        "/DX64Application=$compileX64Root",
        "/DInstallerOutput=$compilerOutputRoot"
    )
    if (-not [string]::IsNullOrWhiteSpace($SignToolName)) {
        $arguments += "/DSignToolName=$SignToolName"
        $arguments += "/S$SignToolName=$SignToolCommand"
    }
    $arguments += (Join-Path $installerSourceSnapshot "installer\PDW.iss")
    & $InnoCompiler @arguments
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }
    if (-not (Test-Path -LiteralPath $temporarySetup -PathType Leaf)) {
        throw "Inno Setup did not produce the expected Setup executable."
    }

    # Inno reads the staged trees during compilation. Revalidate every byte
    # afterward so a concurrent or accidental mutation fails the build.
    Assert-PdwInstallerInputManifest $compileWin32Root "Win32" $sourceCommit
    Assert-PdwInstallerInputManifest $compileX64Root "x64" $sourceCommit
    Assert-PdwInstallerInputManifest $win32Root "Win32" $sourceCommit
    Assert-PdwInstallerInputManifest $x64Root "x64" $sourceCommit
    if ((Get-FileHash -LiteralPath $win32Manifest -Algorithm SHA256).Hash -cne
            $win32ManifestHash -or
        (Get-FileHash -LiteralPath $x64Manifest -Algorithm SHA256).Hash -cne
            $x64ManifestHash) {
        throw "Original installer input changed while Setup was compiled from its immutable snapshot."
    }
    # Complete every mutable source/input gate before evaluating the candidate
    # artifact policy. Thereafter, bind every signature/Defender claim to one
    # exact SHA-256 content identity and recheck it immediately before publish.
    Assert-PdwInstallerInputManifest $compileWin32Root "Win32" $sourceCommit
    Assert-PdwInstallerInputManifest $compileX64Root "x64" $sourceCommit
    Assert-PdwInstallerInputManifest $win32Root "Win32" $sourceCommit
    Assert-PdwInstallerInputManifest $x64Root "x64" $sourceCommit
    Assert-PdwExactCleanGitHead $sourceRoot $sourceCommit "validating Setup policy"
    $candidateHash = (Get-FileHash -LiteralPath $temporarySetup -Algorithm SHA256).Hash
    $signature = Get-AuthenticodeSignature -LiteralPath $temporarySetup
    if ($RequireSignature -and
        $signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "Setup does not have a valid trusted Authenticode signature: $($signature.Status)"
    }
    if ($env:PDW_RELEASE_TEST_MUTATE_SETUP_AFTER_POLICY -eq "1") {
        [System.IO.File]::AppendAllText($temporarySetup, "test mutation",
            [System.Text.UTF8Encoding]::new($false))
    }
    if ((Get-FileHash -LiteralPath $temporarySetup -Algorithm SHA256).Hash -cne
        $candidateHash) {
        throw "Setup changed while its Authenticode policy was evaluated."
    }
    if ($ScanWithDefender) {
        $defender = Join-Path $env:ProgramFiles "Windows Defender\MpCmdRun.exe"
        if (-not (Test-Path -LiteralPath $defender -PathType Leaf)) {
            throw "Microsoft Defender command-line scanner was not found."
        }
        & $defender -Scan -ScanType 3 -File $temporarySetup -DisableRemediation
        if ($LASTEXITCODE -ne 0) {
            throw "Microsoft Defender did not return a clean result for Setup."
        }
        if ((Get-FileHash -LiteralPath $temporarySetup -Algorithm SHA256).Hash -cne
            $candidateHash) {
            throw "Setup changed while Microsoft Defender scanned it."
        }
    }

    # Keep the public output absent until compiler success, input/source
    # revalidation, signature policy and malware scanning all pass.
    Assert-PdwInstallerInputManifest $compileWin32Root "Win32" $sourceCommit
    Assert-PdwInstallerInputManifest $compileX64Root "x64" $sourceCommit
    Assert-PdwInstallerInputManifest $win32Root "Win32" $sourceCommit
    Assert-PdwInstallerInputManifest $x64Root "x64" $sourceCommit
    Assert-PdwExactCleanGitHead $sourceRoot $sourceCommit "publishing Setup"
    $hash = (Get-FileHash -LiteralPath $temporarySetup -Algorithm SHA256).Hash
    if ($hash -cne $candidateHash) {
        throw "Setup changed after signature or Defender validation."
    }
    [System.IO.File]::WriteAllText($temporarySetupHash,
        "$hash  $([System.IO.Path]::GetFileName($setup))`r`n",
        [System.Text.UTF8Encoding]::new($false))
    $setupSize = (Get-Item -LiteralPath $temporarySetup).Length
    $result = [pscustomobject]@{
        Setup = $setup
        Size = $setupSize
        SHA256 = $hash
        Signature = $signature.Status
        DefenderScanned = [bool]$ScanWithDefender
    }

    # Publish the executable last. A failure before this move cannot leave a
    # candidate Setup under its release filename.
    if ($env:PDW_RELEASE_TEST_OCCUPY_SETUP_DESTINATION -eq "1") {
        New-Item -ItemType Directory -Path $finalSet | Out-Null
        [System.IO.File]::WriteAllText($setup, "operator-owned setup`n",
            [System.Text.UTF8Encoding]::new($false))
    }
    if (Test-Path -LiteralPath $finalSet) {
        throw "Installer output became occupied before publication; it was left untouched: $setup"
    }
    $publicationSet = Join-Path $compilerOutputRoot $setName
    New-Item -ItemType Directory -Path $publicationSet | Out-Null
    Move-PdwFileNoReplace $temporarySetupHash (Join-Path $publicationSet "$setupName.sha256")
    Move-PdwFileNoReplace $temporarySetup (Join-Path $publicationSet $setupName)
    # The complete Setup+checksum set is already content/signature validated.
    # Publish it with one atomic no-replace directory rename as the final
    # fallible release operation.
    Move-PdwDirectoryNoReplace $publicationSet $finalSet
    $publishedSet = $true
    $result
}
finally {
    foreach ($privateRoot in @(
        $installerSourceSnapshot,
        $installerInputSnapshot,
        $compilerOutputRoot
    )) {
        if (Test-Path -LiteralPath $privateRoot) {
            try {
                Remove-Item -LiteralPath $privateRoot -Recurse -Force
            }
            catch {
                if ($publishedSet) {
                    Write-Warning "Setup was published, but a private build directory could not be removed: $privateRoot"
                }
                else {
                    throw
                }
            }
        }
    }
}
