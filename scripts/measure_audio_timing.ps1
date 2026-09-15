<#
.SYNOPSIS
Capture and summarize [DIAG C1] audio render timing from a Pico 2 running Pico2Seq.

.DESCRIPTION
Opens the board's USB CDC serial port at 115200 baud, records [DIAG C1] heartbeat
windows for -Seconds, and prints median/mean/min/max render_us, the peak max_us,
and the increases in over/underruns/txstalls, plus a machine-readable RESULT line.
Use it on the RAM and XIP (-AudioInFlash) firmware builds with identical device
settings to compare hot-code placement, per docs/audio-performance.md.

-AnalyzeFile re-summarizes a log saved earlier with -RawLog, without a board.
-UploadDir optionally flashes a build directory first via arduino-cli.

.EXAMPLE
./scripts/measure_audio_timing.ps1 -UploadDir build/audio-ram-150 -Label ram -Seconds 60 -RawLog build/audio-ram-150/diag.log
./scripts/measure_audio_timing.ps1 -UploadDir build/audio-xip-150 -Label xip -Seconds 60 -RawLog build/audio-xip-150/diag.log
./scripts/measure_audio_timing.ps1 -AnalyzeFile build/audio-ram-150/diag.log -Label ram-recheck
#>
[CmdletBinding()]
param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$Port,
    [int]$Seconds = 30,
    [string]$Label = 'run',
    [string]$UploadDir,
    [string]$RawLog,
    [string]$AnalyzeFile
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$baud = 115200     # kSerialBaud, src/app/Application.cpp
$windowMs = 2000   # kDiagnosticIntervalMs: one [DIAG C1] window per interval

function ConvertTo-Heartbeat {
    param([string]$Line)

    if ($Line -match '\[DIAG C1\].*render_us=(\d+) max_us=(\d+) budget_us=(\d+) over=(\d+) underruns=(\d+) txstalls=(\d+)') {
        [pscustomobject]@{
            RenderUs  = [double]$Matches[1]
            MaxUs     = [double]$Matches[2]
            Over      = [long]$Matches[4]
            Underruns = [long]$Matches[5]
            TxStalls  = [long]$Matches[6]
        }
    } else {
        $null
    }
}

function Get-Median {
    param([double[]]$Values)

    $sorted = $Values | Sort-Object
    $mid = [int][math]::Floor(($sorted.Count - 1) / 2)
    if ($sorted.Count % 2 -eq 1) {
        $sorted[$mid]
    } else {
        ($sorted[$mid] + $sorted[$mid + 1]) / 2
    }
}

function Show-Summary {
    param([object[]]$Heartbeats, [string]$RunLabel)

    $renders = @($Heartbeats | ForEach-Object { $_.RenderUs })
    $peaks = @($Heartbeats | ForEach-Object { $_.MaxUs })
    $first = $Heartbeats[0]
    $last = $Heartbeats[-1]
    $median = Get-Median -Values $renders
    $mean = [math]::Round(($renders | Measure-Object -Average).Average, 1)
    $min = [double]($renders | Measure-Object -Minimum).Minimum
    $max = [double]($renders | Measure-Object -Maximum).Maximum
    $peak = [double]($peaks | Measure-Object -Maximum).Maximum

    Write-Host ("== [{0}] {1} heartbeat windows (~{2} s of audio) ==" -f $RunLabel, $Heartbeats.Count, [int]($Heartbeats.Count * $windowMs / 1000))
    Write-Host ("  render_us : median {0}  mean {1}  min {2}  max {3}  (budget 5333)" -f $median, $mean, $min, $max)
    Write-Host ("  max_us    : peak {0}" -f $peak)
    Write-Host ("  over      : {0} -> {1}  (+{2})" -f $first.Over, $last.Over, ($last.Over - $first.Over))
    Write-Host ("  underruns : {0} -> {1}  (+{2})" -f $first.Underruns, $last.Underruns, ($last.Underruns - $first.Underruns))
    Write-Host ("  txstalls  : {0} -> {1}  (+{2})" -f $first.TxStalls, $last.TxStalls, ($last.TxStalls - $first.TxStalls))
    Write-Host ("RESULT label={0} windows={1} render_us_median={2} render_us_mean={3} render_us_min={4} render_us_max={5} max_us_peak={6} over_delta={7} underruns_delta={8} txstalls_delta={9}" -f `
        $RunLabel, $Heartbeats.Count, $median, $mean, $min, $max, $peak, `
        ($last.Over - $first.Over), ($last.Underruns - $first.Underruns), ($last.TxStalls - $first.TxStalls))
}

function Find-PicoPort {
    foreach ($line in @(& $ArduinoCli board list 2>$null)) {
        if ($line -match '^(COM\d+)') {
            return $Matches[1]
        }
    }
    return $null
}

# Offline mode: summarize a previously captured log.
if (-not [string]::IsNullOrWhiteSpace($AnalyzeFile)) {
    $heartbeats = @(Get-Content -LiteralPath $AnalyzeFile | ForEach-Object { ConvertTo-Heartbeat -Line $_ } | Where-Object { $null -ne $_ })
    if ($heartbeats.Count -eq 0) {
        throw "No [DIAG C1] lines found in '$AnalyzeFile'."
    }
    Show-Summary -Heartbeats $heartbeats -RunLabel $Label
    exit 0
}

$portName = if ([string]::IsNullOrWhiteSpace($Port)) { Find-PicoPort } else { $Port }
if ([string]::IsNullOrWhiteSpace($portName)) {
    throw "No Pico serial port found. Connect the board and check 'arduino-cli board list', or pass -Port COMx. First flash from BOOTSEL mode: copy Pico2Seq.ino.uf2 onto the RP2350 drive, then rerun without -UploadDir."
}

if (-not [string]::IsNullOrWhiteSpace($UploadDir)) {
    $uploadPath = [IO.Path]::GetFullPath($UploadDir)
    if (-not (Test-Path -LiteralPath (Join-Path $uploadPath 'Pico2Seq.ino.uf2') -PathType Leaf)) {
        throw "No Pico2Seq.ino.uf2 in '$uploadPath'."
    }
    Write-Host "Uploading $uploadPath to $portName ..."
    & $ArduinoCli upload -p $portName --input-dir $uploadPath
    if ($LASTEXITCODE -ne 0) {
        throw "Upload failed (exit code $LASTEXITCODE). If the board is wedged, hold BOOTSEL while replugging and copy Pico2Seq.ino.uf2 onto the RP2350 drive."
    }
    Write-Host 'Upload OK; waiting for the firmware to reboot and USB CDC to re-enumerate ...'
    Start-Sleep -Seconds 10
}

$serial = $null
for ($attempt = 1; $attempt -le 3 -and $null -eq $serial; $attempt++) {
    try {
        $serial = New-Object System.IO.Ports.SerialPort $portName, $baud, 'None', 8, 'One'
        $serial.DtrEnable = $true
        $serial.RtsEnable = $true
        $serial.ReadTimeout = 500
        $serial.Open()
    } catch {
        if ($null -ne $serial) { $serial.Dispose() }
        $serial = $null
        if ($attempt -eq 3) {
            throw "Could not open $portName at $baud baud: $($_.Exception.Message)"
        }
        Start-Sleep -Seconds 2
    }
}

$rawPath = $null
if (-not [string]::IsNullOrWhiteSpace($RawLog)) {
    $rawPath = [IO.Path]::GetFullPath($RawLog)
    $rawDir = Split-Path -Parent $rawPath
    if ($rawDir -and -not (Test-Path -LiteralPath $rawDir)) {
        New-Item -ItemType Directory -Path $rawDir -Force | Out-Null
    }
    Remove-Item -LiteralPath $rawPath -ErrorAction SilentlyContinue
}

$heartbeats = @()
try {
    Write-Host "Capturing [DIAG C1] heartbeats from $portName for $Seconds s (label '$Label') ..."
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $serial.ReadLine()
        } catch [TimeoutException] {
            continue
        }
        $trimmed = $line.Trim()
        if (-not $trimmed) { continue }
        Write-Host "  $trimmed"
        if ($null -ne $rawPath) {
            Add-Content -LiteralPath $rawPath -Value $trimmed
        }
        $beat = ConvertTo-Heartbeat -Line $trimmed
        if ($null -ne $beat) {
            $heartbeats += $beat
        }
    }
} finally {
    $serial.Close()
    $serial.Dispose()
}

if ($heartbeats.Count -eq 0) {
    throw "No [DIAG C1] lines received in $Seconds s. Confirm the firmware is running and its USB serial port is $portName."
}
Show-Summary -Heartbeats $heartbeats -RunLabel $Label
