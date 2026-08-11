Set-StrictMode -Version Latest

$script:PdwInstallerInputManifestName = "PDW_INSTALLER_INPUT_SHA256SUMS.txt"

function Test-PdwPrivateTransactionArtifactName([string]$Name) {
    $leaf = [System.IO.Path]::GetFileName($Name)
    return (
        $leaf -match '(?i)\.pre-adelaide-flex-.*$' -or
        $leaf -match '(?i)^P(?:AP|DS).*\.tmp$' -or
        $leaf -match '(?i)\.operator-swap.*$'
    )
}

function Get-PdwSensitiveIniFieldName([string]$Line) {
    $match = [regex]::Match(
        $Line,
        '^\s*(Password|Token|Secret|ApiKey|BearerToken|DeviceEndpointId)\s*=\s*\S+',
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
    )
    if ($match.Success) {
        return $match.Groups[1].Value
    }
    return $null
}

function Write-PdwDirectoryHashManifest(
    [string]$Directory,
    [string]$ManifestName = "SHA256SUMS.txt"
) {
    $root = (Resolve-Path -LiteralPath $Directory).Path
    $manifest = Join-Path $root $ManifestName
    if (Test-Path -LiteralPath $manifest) {
        throw "Directory hash manifest already exists: $manifest"
    }
    $lines = @()
    foreach ($file in @(Get-ChildItem -LiteralPath $root -Recurse -Force -File |
            Sort-Object FullName)) {
        $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
        if ($relative -match '[\r\n]' -or
            $relative -match '(^|/)\.\.?(/|$)' -or
            $relative.StartsWith('/') -or
            $relative -ceq $ManifestName) {
            throw "Directory contains an unsafe manifest path: $relative"
        }
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).
            Hash.ToLowerInvariant()
        $lines += "$hash  $relative"
    }
    if ($lines.Count -eq 0) {
        throw "Cannot manifest an empty directory: $root"
    }
    [System.IO.File]::WriteAllLines($manifest, $lines,
        [System.Text.UTF8Encoding]::new($false))
}

function Assert-PdwDirectoryHashManifest(
    [string]$Directory,
    [string]$ManifestName = "SHA256SUMS.txt"
) {
    $root = (Resolve-Path -LiteralPath $Directory).Path
    $manifest = Join-Path $root $ManifestName
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "Directory hash manifest is missing: $manifest"
    }
    $listedPaths = @()
    $seen = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($line in @(Get-Content -LiteralPath $manifest)) {
        $match = [regex]::Match($line, '^([0-9a-f]{64})  (.+)$')
        if (-not $match.Success) {
            throw "Directory hash manifest has a malformed entry."
        }
        $expectedHash = $match.Groups[1].Value
        $relative = $match.Groups[2].Value
        if ($relative -match '[\\\r\n]' -or
            $relative -match '(^|/)\.\.?(/|$)' -or
            $relative.StartsWith('/') -or
            $relative -ceq $ManifestName -or
            -not $seen.Add($relative)) {
            throw "Directory hash manifest contains an unsafe or duplicate path: $relative"
        }
        $path = Join-Path $root $relative.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Manifested directory file is missing: $relative"
        }
        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).
            Hash.ToLowerInvariant()
        if ($actualHash -cne $expectedHash) {
            throw "Directory failed SHA-256 validation: $relative"
        }
        $listedPaths += $relative
    }
    if ($listedPaths.Count -eq 0) {
        throw "Directory hash manifest is empty."
    }
    $presentPaths = @(Get-ChildItem -LiteralPath $root -Recurse -Force -File |
        Where-Object { $_.FullName -cne $manifest } |
        ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\', '/') } |
        Sort-Object)
    $listedPaths = @($listedPaths | Sort-Object)
    if (($presentPaths -join "`n") -cne ($listedPaths -join "`n")) {
        throw "Directory contains an unlisted file or omits a manifested file."
    }
}

function Get-PdwExactCleanGitHead([string]$SourceRoot, [string]$Operation) {
    $root = (Resolve-Path -LiteralPath $SourceRoot).Path
    $gitRootOutput = @(& git -C $root rev-parse --show-toplevel)
    $gitRootResult = $LASTEXITCODE
    $gitRoot = ($gitRootOutput -join "`n").Trim()
    if ($gitRootResult -ne 0 -or [string]::IsNullOrWhiteSpace($gitRoot)) {
        throw "Unable to resolve the Git root before $Operation."
    }
    $resolvedGitRoot = (Resolve-Path -LiteralPath $gitRoot).Path
    if (-not [string]::Equals($resolvedGitRoot, $root,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "The $Operation source must be the exact Git checkout root: $root"
    }

    $status = @(& git -C $root status --porcelain --untracked-files=all)
    if ($LASTEXITCODE -ne 0) { throw "Unable to inspect Git status before $Operation." }
    if ($status.Count -ne 0) {
        throw "The source tree must be clean before $Operation.`n$($status -join "`n")"
    }

    $commitOutput = @(& git -C $root rev-parse --verify HEAD)
    $commitResult = $LASTEXITCODE
    $commit = ($commitOutput -join "`n").Trim().ToLowerInvariant()
    if ($commitResult -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') {
        throw "Unable to resolve the exact source commit before $Operation."
    }
    return $commit
}

function Assert-PdwExactCleanGitHead(
    [string]$SourceRoot,
    [string]$ExpectedCommit,
    [string]$Operation
) {
    $currentCommit = Get-PdwExactCleanGitHead $SourceRoot $Operation
    if ($currentCommit -cne $ExpectedCommit.ToLowerInvariant()) {
        throw "Git HEAD changed while $Operation; expected $ExpectedCommit but found $currentCommit."
    }
}

function New-PdwGitCommitSnapshot(
    [string]$SourceRoot,
    [string]$Commit,
    [string]$Destination
) {
    if ($Commit -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Cannot snapshot an invalid Git commit ID."
    }
    if (Test-Path -LiteralPath $Destination) {
        throw "Git snapshot destination already exists: $Destination"
    }
    $destinationParent = Split-Path -Parent ([System.IO.Path]::GetFullPath($Destination))
    if (-not (Test-Path -LiteralPath $destinationParent -PathType Container)) {
        New-Item -ItemType Directory -Path $destinationParent | Out-Null
    }
    $archive = Join-Path $destinationParent ("pdw-git-snapshot-" +
        [guid]::NewGuid().ToString("N") + ".zip")
    try {
        & git -C $SourceRoot archive --format=zip "--output=$archive" $Commit
        if ($LASTEXITCODE -ne 0 -or
            -not (Test-Path -LiteralPath $archive -PathType Leaf)) {
            throw "Unable to create an immutable Git archive for commit $Commit."
        }
        New-Item -ItemType Directory -Path $Destination | Out-Null
        Expand-Archive -LiteralPath $archive -DestinationPath $Destination
        if (Test-Path -LiteralPath (Join-Path $Destination ".git")) {
            throw "The immutable release snapshot unexpectedly contains .git."
        }
    }
    catch {
        if (Test-Path -LiteralPath $Destination) {
            Remove-Item -LiteralPath $Destination -Recurse -Force
        }
        throw
    }
    finally {
        if (Test-Path -LiteralPath $archive) {
            Remove-Item -LiteralPath $archive -Force
        }
    }
}

function Copy-PdwFileHashStable([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required release input is missing: $Source"
    }
    if (Test-Path -LiteralPath $Destination) {
        throw "Release staging destination already exists: $Destination"
    }
    $before = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $Source -Destination $Destination
    $copied = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    $after = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    if ($before -cne $copied -or $before -cne $after) {
        throw "Release input changed while it was copied: $Source"
    }
}

function Move-PdwDirectoryNoReplace([string]$Source, [string]$Destination) {
    $sourcePath = [System.IO.Path]::GetFullPath($Source)
    $destinationPath = [System.IO.Path]::GetFullPath($Destination)
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
        throw "Release directory candidate is missing: $sourcePath"
    }
    if ((Test-Path -LiteralPath $destinationPath) -or
        [System.IO.File]::Exists($destinationPath) -or
        [System.IO.Directory]::Exists($destinationPath)) {
        throw "Release destination already exists; refusing to merge or overwrite it: $destinationPath"
    }
    try {
        # Directory.Move is an atomic, no-replace rename on the same volume. In
        # contrast, Move-Item can merge into a directory created after preflight.
        [System.IO.Directory]::Move($sourcePath, $destinationPath)
    }
    catch {
        if (Test-Path -LiteralPath $destinationPath) {
            throw "Release destination became occupied; it was left untouched: $destinationPath"
        }
        throw
    }
}

function Move-PdwFileNoReplace([string]$Source, [string]$Destination) {
    $sourcePath = [System.IO.Path]::GetFullPath($Source)
    $destinationPath = [System.IO.Path]::GetFullPath($Destination)
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Release file candidate is missing: $sourcePath"
    }
    if ((Test-Path -LiteralPath $destinationPath) -or
        [System.IO.File]::Exists($destinationPath) -or
        [System.IO.Directory]::Exists($destinationPath)) {
        throw "Release destination already exists; refusing to overwrite it: $destinationPath"
    }
    try {
        # File.Move without an overwrite argument is the matching no-replace
        # primitive on Windows PowerShell/.NET Framework.
        [System.IO.File]::Move($sourcePath, $destinationPath)
    }
    catch {
        if (Test-Path -LiteralPath $destinationPath) {
            throw "Release destination became occupied; it was left untouched: $destinationPath"
        }
        throw
    }
}

function Write-PdwInstallerInputManifest(
    [string]$Directory,
    [ValidateSet("Win32", "x64")]
    [string]$Architecture,
    [string]$Commit
) {
    if ($Commit -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Cannot write an installer-input manifest for an invalid commit ID."
    }
    $root = (Resolve-Path -LiteralPath $Directory).Path
    $manifest = Join-Path $root $script:PdwInstallerInputManifestName
    if (Test-Path -LiteralPath $manifest) {
        throw "Installer-input manifest already exists: $manifest"
    }
    $lines = @(
        "PDW-INSTALLER-INPUT-MANIFEST v1",
        "commit=$($Commit.ToLowerInvariant())",
        "architecture=$Architecture"
    )
    $files = @(Get-ChildItem -LiteralPath $root -Recurse -Force -File |
        Sort-Object FullName)
    if ($files.Count -eq 0) {
        throw "Installer input is empty: $root"
    }
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
        if ($relative -match '[\r\n]' -or
            $relative -match '(^|/)\.\.?(/|$)' -or
            $relative.StartsWith('/') -or
            $relative -ceq $script:PdwInstallerInputManifestName) {
            throw "Installer input contains an unsafe manifest path: $relative"
        }
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).
            Hash.ToLowerInvariant()
        $lines += "$hash  $relative"
    }
    [System.IO.File]::WriteAllLines($manifest, $lines,
        [System.Text.UTF8Encoding]::new($false))
}

function Assert-PdwInstallerInputManifest(
    [string]$Directory,
    [ValidateSet("Win32", "x64")]
    [string]$Architecture,
    [string]$ExpectedCommit
) {
    if ($ExpectedCommit -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Cannot validate installer input against an invalid commit ID."
    }
    $root = (Resolve-Path -LiteralPath $Directory).Path
    $manifest = Join-Path $root $script:PdwInstallerInputManifestName
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "Installer-input SHA-256 manifest is missing: $manifest"
    }
    $lines = @(Get-Content -LiteralPath $manifest)
    if ($lines.Count -lt 4 -or
        $lines[0] -cne "PDW-INSTALLER-INPUT-MANIFEST v1" -or
        $lines[1] -cne "commit=$($ExpectedCommit.ToLowerInvariant())" -or
        $lines[2] -cne "architecture=$Architecture") {
        throw "Installer-input manifest identity is malformed or does not match $Architecture/$ExpectedCommit."
    }

    $listedPaths = @()
    $seen = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    for ($index = 3; $index -lt $lines.Count; ++$index) {
        $match = [regex]::Match($lines[$index], '^([0-9a-f]{64})  (.+)$')
        if (-not $match.Success) {
            throw "Installer-input manifest has a malformed SHA-256 entry."
        }
        $expectedHash = $match.Groups[1].Value
        $relative = $match.Groups[2].Value
        if ($relative -match '[\\\r\n]' -or
            $relative -match '(^|/)\.\.?(/|$)' -or
            $relative.StartsWith('/') -or
            $relative -ceq $script:PdwInstallerInputManifestName -or
            -not $seen.Add($relative)) {
            throw "Installer-input manifest contains an unsafe or duplicate path: $relative"
        }
        $path = Join-Path $root $relative.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Installer-input manifest file is missing: $relative"
        }
        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).
            Hash.ToLowerInvariant()
        if ($actualHash -cne $expectedHash) {
            throw "Installer input failed SHA-256 validation: $relative"
        }
        $listedPaths += $relative
    }

    $presentPaths = @(Get-ChildItem -LiteralPath $root -Recurse -Force -File |
        Where-Object { $_.FullName -cne $manifest } |
        ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\', '/') } |
        Sort-Object)
    $listedPaths = @($listedPaths | Sort-Object)
    if (($presentPaths -join "`n") -cne ($listedPaths -join "`n")) {
        throw "Installer input contains an unlisted file or omits a manifested file."
    }
}
