$title = Read-Host 'Build title (for example: fasti2c)'
if ([string]::IsNullOrWhiteSpace($title) -or $title -match '[\\/:*?"<>|]') {
    throw 'Build title must be non-empty and cannot contain path-invalid characters.'
}

$cpuMHz = [int](Read-Host 'CPU speed in MHz (150, 225, or 300)')
if ($cpuMHz -notin 150, 225, 300) {
    throw 'CPU speed must be 150, 225, or 300 MHz.'
}

$buildDirectory = Join-Path $PWD ("build\{0}-{1}" -f $title, $cpuMHz)
$logFile = Join-Path $PWD ("build\{0}-{1}-build.log" -f $title, $cpuMHz)

& scripts/build_pico2seq.ps1 `
    -CpuMHz $cpuMHz `
    -BuildDirectory $buildDirectory `
    -KeepStage *> $logFile

$buildExit = $LASTEXITCODE
Get-Content $logFile -Tail 28

if ($buildExit -ne 0) {
    exit $buildExit
}

$ports = @(
    (& arduino-cli board list --format json | ConvertFrom-Json).detected_ports |
        Where-Object {
            $_.matching_boards.fqbn -contains 'rp2040:rp2040:rpipico2'
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
    'flash=4194304_0'
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
