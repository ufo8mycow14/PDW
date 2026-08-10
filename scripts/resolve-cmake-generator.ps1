[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "Required build tool 'cmake' was not found. See Readme > Build on Windows."
}

$programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer's vswhere.exe was not found. Install Visual Studio Desktop development with C++."
}

$installationVersion = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($installationVersion)) {
    throw "Visual Studio C++ x86/x64 build tools were not found."
}

$visualStudioMajor = ([version]$installationVersion).Major
$generatorPattern = "^\*?\s*(Visual Studio $visualStudioMajor \d{4})\s*="
$generator = cmake --help |
    Select-String -Pattern $generatorPattern |
    Select-Object -First 1 |
    ForEach-Object { $_.Matches[0].Groups[1].Value }

if ([string]::IsNullOrWhiteSpace($generator)) {
    throw "CMake does not provide a generator for installed Visual Studio major version $visualStudioMajor. Update CMake or install a supported Visual Studio C++ toolset."
}

$generator
