[CmdletBinding()]
param(
    [ValidateSet(17, 18)]
    [int]$VisualStudioMajor = 18
)

$ErrorActionPreference = "Stop"

function Repair-ProcessPathEnvironment {
    $processEnvironment = [Environment]::GetEnvironmentVariables(
        [EnvironmentVariableTarget]::Process
    )
    $pathVariables = @(
        $processEnvironment.GetEnumerator() |
            Where-Object {
                [string]::Equals(
                    [string]$_.Key,
                    "Path",
                    [StringComparison]::OrdinalIgnoreCase
                )
            }
    )

    if ($pathVariables.Count -le 1 -and
        ($pathVariables.Count -eq 0 -or [string]$pathVariables[0].Key -ceq "Path")) {
        return
    }

    $pathSegments = [Collections.Generic.List[string]]::new()
    $seenSegments = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase
    )
    foreach ($pathVariable in $pathVariables) {
        foreach ($segment in ([string]$pathVariable.Value -split ';')) {
            $trimmedSegment = $segment.Trim()
            if (-not [string]::IsNullOrWhiteSpace($trimmedSegment) -and
                $seenSegments.Add($trimmedSegment)) {
                $pathSegments.Add($trimmedSegment)
            }
        }
    }

    foreach ($pathVariable in $pathVariables) {
        if ([string]$pathVariable.Key -cne "Path") {
            [Environment]::SetEnvironmentVariable(
                [string]$pathVariable.Key,
                $null,
                [EnvironmentVariableTarget]::Process
            )
        }
    }
    [Environment]::SetEnvironmentVariable(
        "Path",
        ($pathSegments -join ';'),
        [EnvironmentVariableTarget]::Process
    )
}

Repair-ProcessPathEnvironment

$programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer's vswhere.exe was not found. Install Visual Studio Desktop development with C++."
}

$versionRange = "[$VisualStudioMajor.0,$($VisualStudioMajor + 1).0)"
$visualStudioRoot = (& $vswhere -latest -products * -version $versionRange `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($visualStudioRoot)) {
    throw "Visual Studio $VisualStudioMajor C++ x86/x64 build tools were not found."
}

$generatorPattern = "^\*?\s*(Visual Studio $VisualStudioMajor \d{4})\s*="
$cmakeCandidates = @()
$pathCmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($pathCmake) {
    $cmakeCandidates += $pathCmake.Source
}
$cmakeCandidates += Join-Path $visualStudioRoot `
    "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

$selectedCmake = $null
$generator = $null
foreach ($candidate in $cmakeCandidates | Select-Object -Unique) {
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        continue
    }
    $candidateGenerator = & $candidate --help |
        Select-String -Pattern $generatorPattern |
        Select-Object -First 1 |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
    if (-not [string]::IsNullOrWhiteSpace($candidateGenerator)) {
        $selectedCmake = $candidate
        $generator = $candidateGenerator
        break
    }
}

if ([string]::IsNullOrWhiteSpace($generator)) {
    throw "CMake does not provide a generator for Visual Studio major version $VisualStudioMajor. Update CMake or install the matching Visual Studio CMake component."
}

$cmakeDirectory = Split-Path -Parent $selectedCmake
if (-not (($env:Path -split ';') -contains $cmakeDirectory)) {
    $env:Path = "$cmakeDirectory;$env:Path"
}

$generator
