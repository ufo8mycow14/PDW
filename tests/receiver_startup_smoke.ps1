[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$MockReceiver,
    [Parameter(Mandatory = $true)][string]$TestRoot,
    [string]$FlexFixture,
    [string]$PocsagFixture,
    [switch]$RecoveryCycle
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$mockPath = (Resolve-Path -LiteralPath $MockReceiver).Path
$sourceRoot = Split-Path -Parent $PSScriptRoot
$root = Join-Path ([IO.Path]::GetFullPath($TestRoot)) ([guid]::NewGuid().ToString('N'))
$previousReady = [Environment]::GetEnvironmentVariable('PDW_TEST_RTL_READY_FILE', 'Process')
$previousMarker = [Environment]::GetEnvironmentVariable('PDW_TEST_RTL_CALLBACK_MARKER', 'Process')
$previousFixture = [Environment]::GetEnvironmentVariable('PDW_TEST_RTL_SYNTHETIC_IQ', 'Process')
$previousTiming = [Environment]::GetEnvironmentVariable('PDW_TEST_RTL_CADENCE', 'Process')
$controlVariables = 'PDW_TEST_RTL_CLOSE_REQUEST','PDW_TEST_RTL_CRASH_REQUEST','PDW_TEST_RTL_STALL_ONCE','PDW_TEST_RTL_STALL_AFTER_IQ'
$previousControls = @{}
foreach ($name in $controlVariables) {
    $previousControls[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
    [Environment]::SetEnvironmentVariable($name,$null,'Process')
}

function Wait-FixtureRows($MarkerPath, $LogPath, $ExpectedRows, [int]$PreviousId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(25)
    do {
        Start-Sleep -Milliseconds 150
        $observedId = 0
        $rows = @()
        if ((Test-Path -LiteralPath $MarkerPath) -and (Test-Path -LiteralPath $LogPath)) {
            [void][int]::TryParse(([IO.File]::ReadAllText($MarkerPath)),[ref]$observedId)
            $rows = @(Get-Content -LiteralPath $LogPath | Where-Object { $_.Length -ne 0 })
        }
        if ($observedId -gt 0 -and $observedId -ne $PreviousId -and ($ExpectedRows -join "`n") -ceq ($rows -join "`n")) { return $observedId }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Relaunch did not produce the expected $($ExpectedRows.Count) exact synthetic rows (observed $($rows.Count))."
}

function Close-Fixture($Application, $ClosePath) {
    $null = $Application.Handle
    [IO.File]::WriteAllText($ClosePath,'close synthetic receiver normally')
    if (!$Application.WaitForExit(10000)) { throw 'The synthetic receiver did not release its process within ten seconds.' }
    if ($Application.ExitCode -ne 0) { throw "The synthetic receiver did not report a clean exit (code=$($Application.ExitCode))." }
}

function Get-FixtureProcessIds([string]$ImagePath) {
    $imageName = [IO.Path]::GetFileName($ImagePath).Replace("'", "\'")
    Get-CimInstance -ClassName Win32_Process -Filter "Name = '$imageName'" -Property ProcessId,ExecutablePath |
        Where-Object { $_.ExecutablePath -eq $ImagePath } | Select-Object -ExpandProperty ProcessId
}

function Assert-FixtureIdleHealth($Application, [int]$ExpectedRows) {
    $mapping = [IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew("Local\PDW.ReceiverHealth.$($Application.Id)", 1024)
    $view = $null
    try {
        $view = $mapping.CreateViewAccessor(0, 1024, [IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
        $bytes = [byte[]]::new(1020)
        $first = $last = $null
        $lastStamp = 0
        $clock = [Diagnostics.Stopwatch]::StartNew()
        while ($clock.Elapsed.TotalSeconds -lt 10) {
            Start-Sleep -Milliseconds 200
            if ($Application.HasExited) { throw 'The synthetic receiver exited during idle IQ.' }
            $stamp = $view.ReadInt32(0)
            if (!$stamp -or $stamp -eq $lastStamp -or ($stamp -band 1)) { continue }
            [void]$view.ReadArray(4, $bytes, 0, $bytes.Length)
            [Threading.Thread]::MemoryBarrier()
            if ($view.ReadInt32(0) -ne $stamp) { continue }
            $length = [Array]::IndexOf($bytes, [byte]0)
            if ($length -le 0) { throw 'Invalid synthetic receiver health frame.' }
            $last = [Text.Encoding]::ASCII.GetString($bytes, 0, $length) | ConvertFrom-Json
            if ($last.pid -ne $Application.Id) { throw 'Unexpected synthetic receiver health publisher.' }
            if ($null -eq $first) { $first = $last }
            $lastStamp = $stamp
        }
        if ($null -eq $first -or $last.samples -le $first.samples -or $last.rows -ne $ExpectedRows -or
            $last.worker -ne 1 -or $last.iqSeen -ne 1 -or $last.quarantined -ne 0 -or $last.drops -ne 0) {
            throw 'Idle synthetic IQ must advance samples without losing the worker, dropping blocks, or adding rows.'
        }
    } finally {
        if ($null -ne $view) { $view.Dispose() }
        $mapping.Dispose()
    }
}

try {
    [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_SYNTHETIC_IQ', $null, 'Process')
    $scenarios = if ($RecoveryCycle) { 'nominal' } elseif ($FlexFixture) { 'fast', 'nominal', 'delayed' } else { 'delayed-device', 'delayed-library' }
    foreach ($scenario in $scenarios) {
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
        if ($FlexFixture) {
            [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_CADENCE', $scenario, 'Process')
            if ($settings -notmatch '(?m)^RtlBandwidthHz=12500\r?$') { throw 'Native test requires the shipping 12500 Hz default' }
            $fixture = Join-Path $directory 'generated-synthetic.iq'
            & $FlexFixture --write-native-iq $fixture
            if ($LASTEXITCODE -ne 0) { throw 'Failed to generate synthetic FLEX' }
            $pocsagMode = if ($RecoveryCycle) { '--append-truncated-iq' } else { '--append-native-iq' }
            & $PocsagFixture $pocsagMode (Join-Path $sourceRoot 'tests\fixtures\pocsag') $fixture
            if ($LASTEXITCODE -ne 0) { throw 'Failed to append synthetic POCSAG' }
            [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_SYNTHETIC_IQ', $fixture, 'Process')
            $settings = $settings.Replace('RtlSignalConditioner=0','RtlSignalConditioner=1').Replace('InvertData=0','InvertData=1')
            $settings = $settings.Replace('ColLogfile=1234567','ColLogfile=7').Replace('LogFileUseDate=1','LogFileUseDate=0')
            $settings = [regex]::Replace($settings, '(?m)^LogFile=\r?$', "LogFile=fixture-rows.log`r`nLogfilePath=$directory")
            if ($RecoveryCycle) {
                $closePath = Join-Path $directory 'request-close'
                $crashPath = Join-Path $directory 'request-crash'
                $stallPath = Join-Path $directory 'stalled-first-open'
                $lateStall = Join-Path $directory 'stall-after-one-callback'
                [Environment]::SetEnvironmentVariable($controlVariables[0],$closePath,'Process')
                [Environment]::SetEnvironmentVariable($controlVariables[1],$crashPath,'Process')
                [Environment]::SetEnvironmentVariable($controlVariables[2],$stallPath,'Process')
                [Environment]::SetEnvironmentVariable($controlVariables[3],$lateStall,'Process')
            }
        } else {
            $settings = $settings.Replace('LogFileEnabled=1', 'LogFileEnabled=0')
        }
        [IO.File]::WriteAllText((Join-Path $directory 'PDW.INI'), $settings, [Text.UTF8Encoding]::new($false))
        $ready = Join-Path $directory 'device-ready.txt'
        $marker = Join-Path $directory 'callback-observed.txt'
        $library = Join-Path $receiverDirectory 'rtlsdr.dll'
        [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_READY_FILE', $ready, 'Process')
        [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_CALLBACK_MARKER', $marker, 'Process')
        if ($FlexFixture) {
            Copy-Item -LiteralPath $mockPath -Destination $library
            [IO.File]::WriteAllText($ready, 'synthetic device ready')
        } elseif ($scenario -eq 'delayed-device') {
            Copy-Item -LiteralPath $mockPath -Destination $library
        } else {
            [IO.File]::WriteAllText($ready, 'synthetic device ready')
        }
        $launch = @{ FilePath = $program; WorkingDirectory = $directory; WindowStyle = 'Hidden'; PassThru = $true }
        if ($scenario -eq 'delayed-device') { $launch.ArgumentList = '/startup' }
        $process = Start-Process @launch
        try {
            if ($FlexFixture) {
                $expected = @(' SYNTHETIC PAGER A: 12345',' SYNTHETIC PAGER B: 12345',' SYNTHETIC PAGER C: 12345',' SYNTHETIC PAGER D: 12345',' PDW SYNTHETIC TEST')
                $log = Join-Path $directory 'fixture-rows.log'
                $deadline = [DateTime]::UtcNow.AddSeconds(25)
                do {
                    Start-Sleep -Milliseconds 200
                    $process.Refresh()
                    if ($process.HasExited) { throw 'Application exited before native fixture verification' }
                    $rows = @()
                    if (Test-Path -LiteralPath $log) {
                        $rows = @(Get-Content -LiteralPath $log | Where-Object { $_.Length -ne 0 })
                    }
                    $finished = (Test-Path -LiteralPath $marker) -and $rows.Count -ge $expected.Count
                } until ($finished -or [DateTime]::UtcNow -ge $deadline)
                if (-not $finished -or ($rows -join "`n") -cne ($expected -join "`n")) {
                    throw "Native $scenario mock receiver: expected five exact synthetic rows; saw $($rows.Count), callback complete: $(Test-Path -LiteralPath $marker)"
                }
                Write-Output "Native $scenario callbacks: four FLEX phase rows and the two-bit-repaired POCSAG row matched exactly at 12500 Hz."
                if ($RecoveryCycle) {
                    if (!(Test-Path -LiteralPath $stallPath)) { throw 'Missing first-open USB stall reproduction.' }
                    Assert-FixtureIdleHealth $process $expected.Count
                    Close-Fixture $process $closePath
                    $firstId = $process.Id
                    $process.Dispose()
                    $launch.WorkingDirectory = $root
                    [IO.File]::WriteAllText($lateStall,'simulate stalled transfer after first nonempty callback')
                    $process = Start-Process @launch
                    $secondId = Wait-FixtureRows $marker $log ($expected * 2) $firstId
                    if ($secondId -ne $process.Id) { throw 'A clean exit unexpectedly relaunched a competing receiver.' }
                    [IO.File]::WriteAllText($crashPath,'terminate only this synthetic receiver with a crash status')
                    if (!$process.WaitForExit(8000) -or $process.ExitCode -ne -1073741819) { throw 'Synthetic abnormal exit was not observed.' }
                    $thirdId = Wait-FixtureRows $marker $log ($expected * 3) $secondId
                    if (@(Get-FixtureProcessIds $program) -notcontains $thirdId) { throw 'Unexpected recovery image.' }
                    $process.Dispose()
                    $process = [Diagnostics.Process]::GetProcessById($thirdId)
                    Close-Fixture $process $closePath
                    Start-Sleep -Seconds 3
                    $survivors = @(Get-FixtureProcessIds $program)
                    if ($survivors.Count) { throw 'A synthetic receiver or recovery watcher survived an intentional exit.' }
                    Write-Output 'Stalled USB recovered; silent IQ stayed live; clean reopen and automatic crash relaunch each produced all five exact rows with the saved 12500 Hz profile.'
                }
                continue
            }
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
    foreach ($name in $controlVariables) { [Environment]::SetEnvironmentVariable($name,$previousControls[$name],'Process') }
    [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_CADENCE', $previousTiming, 'Process')
    [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_SYNTHETIC_IQ', $previousFixture, 'Process')
    [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_READY_FILE', $previousReady, 'Process')
    [Environment]::SetEnvironmentVariable('PDW_TEST_RTL_CALLBACK_MARKER', $previousMarker, 'Process')
}
