[CmdletBinding()]
param([switch]$Upload)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$title = Read-Host 'Build title (for example: fasti2c)'
if ([string]::IsNullOrWhiteSpace($title) -or $title -match '[\\/:*?"<>|]') {
    throw 'Build title must be non-empty and cannot contain path-invalid characters.'
}

$cpuMHz = [int](Read-Host 'CPU speed in MHz (150, 225, or 300)')
if ($cpuMHz -notin 150, 225, 300) {
    throw 'CPU speed must be 150, 225, or 300 MHz.'
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Join-Path $repoRoot 'build'
$buildDirectory = Join-Path $buildRoot ("{0}-{1}" -f $title, $cpuMHz)
$logFile = Join-Path $buildRoot ("{0}-{1}-build.log" -f $title, $cpuMHz)
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

try {
    & (Join-Path $PSScriptRoot 'build_pico2seq.ps1') `
        -CpuMHz $cpuMHz `
        -BuildDirectory $buildDirectory *> $logFile
} catch {
    Get-Content -LiteralPath $logFile -Tail 28
    throw
}
Get-Content -LiteralPath $logFile -Tail 28

if (-not $Upload) {
    exit 0
}

$boardList = & arduino-cli board list --format json
if ($LASTEXITCODE -ne 0) {
    throw "Board detection failed with exit code $LASTEXITCODE."
}
$ports = @(
    ($boardList | ConvertFrom-Json).detected_ports |
        Where-Object {
            $matchingBoards = $_.PSObject.Properties['matching_boards']
            if ($null -ne $matchingBoards -and $null -ne $matchingBoards.Value) {
                @($matchingBoards.Value | Where-Object {
                    $fqbn = $_.PSObject.Properties['fqbn']
                    $null -ne $fqbn -and $fqbn.Value -eq 'rp2040:rp2040:rpipico2'
                }).Count -gt 0
            } else {
                $false
            }
        } |
        ForEach-Object { $_.port.address }
)

if ($ports.Count -eq 0) {
    throw 'No connected Raspberry Pi Pico 2 serial port was detected.'
}

if ($ports.Count -gt 1) {
    throw "More than one Pico 2 was detected: $($ports -join ', ')."
}

$boardOptions = @(
    'flash=4194304_65536'
    'arch=arm'
    "freq=$cpuMHz"
    'opt=Optimize3'
    'profile=Disabled'
    'rtti=Disabled'
    'stackprotect=Disabled'
    'exceptions=Disabled'
    'dbgport=Disabled'
    'dbglvl=None'
    'usbstack=tinyusb'
    'ipbtstack=ipv4only'
    'uploadmethod=default'
) -join ','

Write-Host "Uploading '$title' build to $($ports[0])"
& arduino-cli upload `
    --fqbn 'rp2040:rp2040:rpipico2' `
    --board-options $boardOptions `
    --port $ports[0] `
    --input-dir $buildDirectory

exit $LASTEXITCODE
