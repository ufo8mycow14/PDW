[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$MockReceiver,
    [Parameter(Mandatory = $true)][string]$TestRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$mockPath = (Resolve-Path -LiteralPath $MockReceiver).Path
$sourceRoot = Split-Path -Parent $PSScriptRoot
$root = Join-Path ([IO.Path]::GetFullPath($TestRoot)) ([guid]::NewGuid().ToString('N'))
$previousReady = [Environment]::GetEnvironmentVariable('PDW_TEST_RTL_READY_FILE', 'Process')
$previousMarker = [Environment]::GetEnvironmentVariable('PDW_TEST_RTL_CALLBACK_MARKER', 'Process')
try {
    foreach ($scenario in @('delayed-device', 'delayed-library')) {
        $directory = Join-Path $root $scenario
        $receiverDirectory = Join-Path $directory 'Receivers\RTL-SDR'
        New-Item -ItemType Directory -Path $receiverDirectory -Force | Out-Null
        $program = Join-Path $directory ([IO.Path]::GetFileName($executablePath))
        Copy-Item -LiteralPath $executablePath -Destination $program
        Get-ChildItem -LiteralPath (Split-Path -Parent $executablePath) -File -Filter '*.dll' |
            Where-Object { $_.Name -match '^(concrt|msvcp|vcruntime)' } |
            Copy-Item -Destination $directory
        Copy-Item -LiteralPath (Join-Path $sourceRoot 'Receivers\RTL-SDR\receiver.ini') -Destination $receiverDirectory
        $settings = Get-Content -LiteralPath (Join-Path $sourceRoot 'packaging\PDW.INI') -Raw
        $settings = $settings.Replace('AudioSource=0', 'AudioSource=2').Replace('ConfirmExit=1', 'ConfirmExit=0')
        $settings = $settings.Replace('LogFileEnabled=1', 'LogFileEnabled=0')
        [IO.File]::WriteAllText((Join-Path $directory 'PDW.INI'), $settings, [Text.UTF8Encoding]::new($false))
        $ready = Join-Path $directory 'device-ready.txt'
        $marker = Join-Path $directory 'callback-observed.txt'
        $library = Join-Path $receiverDirectory 'rtlsdr.dll'
        [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_READY_FILE', $ready, 'Process')
        [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_CALLBACK_MARKER', $marker, 'Process')
        if ($scenario -eq 'delayed-device') {
            Copy-Item -LiteralPath $mockPath -Destination $library
        } else {
            [IO.File]::WriteAllText($ready, 'synthetic device ready')
        }
        $launch = @{ FilePath = $program; WorkingDirectory = $directory; WindowStyle = 'Hidden'; PassThru = $true }
        if ($scenario -eq 'delayed-device') { $launch.ArgumentList = '/startup' }
        $process = Start-Process @launch
        try {
            $initialWait = if ($scenario -eq 'delayed-device') { 10 } else { 5 }
            Start-Sleep -Seconds $initialWait
            $process.Refresh()
            if ($process.HasExited) { throw "$scenario exited before receiver availability" }
            if (Test-Path -LiteralPath $marker) { throw "$scenario captured before receiver availability" }
            if ($scenario -eq 'delayed-device') {
                [IO.File]::WriteAllText($ready, 'synthetic device ready')
            } else {
                Copy-Item -LiteralPath $mockPath -Destination $library
            }
            $deadline = [DateTime]::UtcNow.AddSeconds(12)
            while (-not (Test-Path -LiteralPath $marker) -and [DateTime]::UtcNow -lt $deadline) {
                Start-Sleep -Milliseconds 100
                $process.Refresh()
                if ($process.HasExited) { throw "$scenario exited during receiver recovery" }
            }
            if (-not (Test-Path -LiteralPath $marker)) {
                throw "$scenario did not recover automatically at zero PPM; startup may be blocked by a modal error"
            }
            Write-Output "$scenario recovered automatically at zero PPM"
        } finally {
            $process.Refresh()
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction Stop }
            $process.Dispose()
        }
    }
} finally {
    [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_READY_FILE', $previousReady, 'Process')
    [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_CALLBACK_MARKER', $previousMarker, 'Process')
}
