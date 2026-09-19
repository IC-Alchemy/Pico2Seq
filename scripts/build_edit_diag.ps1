[CmdletBinding()]
param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$BuildDirectory,
    [ValidateSet(150, 225, 300)] [int]$CpuMHz = 225,
    [switch]$AudioInFlash,
    [switch]$KeepStage
)

# Builds the firmware with the Core-0 edit-path diagnostics enabled
# (PICO2SEQ_EDIT_DIAG=1 in src/app/ControlIO.cpp). The diagnostics print
# one serial line per state change, Core 0 only:
#
#   [EDITDIAG] armed=0x.. focus=..   (on armed-lane bitmap change)
#   [EDITDIAG] focus=.. step=.. hand=.. (on focus/step/hand change)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/build_edit_diag.ps1
#
# Then flash build_fw_diag/Pico2Seq.ino.uf2 (BOOTSEL drag-drop always works),
# open the serial monitor at 115200 baud, and reproduce the failing gestures:
# hold each param button (Note, Velocity, Filter, Attack, Decay, Octave) in
# turn with a step in edit, turn the encoder, wave the lidar hand, and copy
# the [EDITDIAG] lines back. A normal diag build is NOT for performance use.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sketchFile = Join-Path $repoRoot 'Pico2Seq.ino'

if (-not (Test-Path -LiteralPath $sketchFile -PathType Leaf)) {
    throw "Pico2Seq.ino was not found at '$repoRoot'."
}

$arduinoCliCommand = Get-Command $ArduinoCli -ErrorAction SilentlyContinue
if ($null -eq $arduinoCliCommand) {
    throw "Arduino CLI '$ArduinoCli' was not found on PATH. Install Arduino CLI, or pass -ArduinoCli with its full path."
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stageRoot = Join-Path ([IO.Path]::GetTempPath()) "Pico2Seq-arduino-stage-diag-$stamp"
$stageSketch = Join-Path $stageRoot 'Pico2Seq'
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $buildPath = Join-Path $repoRoot "build_fw_diag"
} else {
    $buildPath = [IO.Path]::GetFullPath($BuildDirectory)
}

function Copy-StageTree {
    param(
        [Parameter(Mandatory)] [string]$Source,
        [Parameter(Mandatory)] [string]$Destination,
        [bool]$IsRepositoryRoot = $false
    )

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        if ($IsRepositoryRoot -and $item.Name -in @('.git', 'build', 'build_test', 'vendor')) {
            continue
        }

        if ($item.PSIsContainer) {
            # Arduino CLI recursively compiles sketch/src, so do not stage library examples.
            if ($item.Name -eq 'examples') {
                continue
            }

            Copy-StageTree -Source $item.FullName -Destination (Join-Path $Destination $item.Name)
        } else {
            Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $Destination $item.Name) -Force
        }
    }
}

$boardOptions = @(
    'flash=4194304_65536'
    'arch=arm'
    "freq=$CpuMHz"
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

$buildSucceeded = $false
$audioInRam = if ($AudioInFlash) { 0 } else { 1 }
try {
    Copy-StageTree -Source $repoRoot -Destination $stageSketch -IsRepositoryRoot $true
    New-Item -ItemType Directory -Path $buildPath -Force | Out-Null

    Write-Host "Building Pico2Seq WITH edit diagnostics (PICO2SEQ_EDIT_DIAG=1)"
    Write-Host "Artifacts: $buildPath"
    Write-Host "CPU clock: $CpuMHz MHz"
    Write-Host "Audio code in RAM: $audioInRam"
    & $arduinoCliCommand.Source compile `
        --fqbn 'rp2040:rp2040:rpipico2' `
        --board-options $boardOptions `
        --warnings all `
        --clean `
        --build-property "build.extra_flags=-ffast-math -DPICO2SEQ_AUDIO_IN_RAM=$audioInRam -DPICO2SEQ_EDIT_DIAG=1" `
        --build-path $buildPath `
        $stageSketch

    if ($LASTEXITCODE -ne 0) {
        throw "Arduino CLI compilation failed with exit code $LASTEXITCODE."
    }

    $artifacts = @('.uf2', '.elf', '.bin', '.map')
    $missingArtifacts = @($artifacts | Where-Object {
        -not (Get-ChildItem -LiteralPath $buildPath -Recurse -File -Filter "*$PSItem" | Select-Object -First 1)
    })
    if ($missingArtifacts.Count -gt 0) {
        throw "Arduino CLI exited with code 0, but these expected artifacts were not found: $($missingArtifacts -join ', ')."
    }

    $buildSucceeded = $true
    Write-Host 'Diag build completed. Flash the .uf2, open serial at 115200 baud, and copy the [EDITDIAG] lines back.'
} finally {
    if (-not $KeepStage) {
        if (Test-Path -LiteralPath $stageRoot) {
            Remove-Item -LiteralPath $stageRoot -Recurse -Force
        }
    } elseif ($buildSucceeded) {
        Write-Host "Staging directory retained: $stageRoot"
    } else {
        Write-Warning "Build failed; staging directory retained for diagnosis: $stageRoot"
    }
}
