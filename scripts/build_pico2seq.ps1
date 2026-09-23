[CmdletBinding()]
param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$BuildDirectory,
    [ValidateSet(150, 225, 300)] [int]$CpuMHz = 150,
    [switch]$AudioInFlash,
    [switch]$KeepStage,
    # User-facing build name (first prompt of build.ps1). publish_uf2.ps1
    # turns it into "<title>_Pico2Seq_<yyyy-MM-dd>.uf2".
    [string]$FirmwareTitle = '',
    # Working-copy destination. Keep this default in sync with
    # publish_uf2.ps1 -WorkingUf2Dir.
    [string]$WorkingUf2Dir = 'Z:\Codezzz\workingUF2',
    [switch]$NoWorkingCopy
)

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
$stageRoot = Join-Path ([IO.Path]::GetTempPath()) "Pico2Seq-arduino-stage-$stamp"
$stageSketch = Join-Path $stageRoot 'Pico2Seq'
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $buildPath = Join-Path $repoRoot "build\arduino-cli\Pico2Seq-current-$stamp"
} else {
    $buildPath = [IO.Path]::GetFullPath($BuildDirectory)
}

$subStatus = git -C $repoRoot submodule status --recursive
# git prefixes each line with ' ' (in sync), '+' (different commit checked out),
# '-' (not initialized) or 'U' (merge conflicts). Only the last three are stale.
if ($subStatus -match '^[\+\-U]') { throw "Submodules out of date. Run: git submodule update --init --recursive`n$subStatus" }
if (-not (Test-Path (Join-Path $repoRoot 'src/rpdsp/src/rpdsp/DSPFunctions.h'))) {
    throw 'src/rpdsp is empty. Clone with --recurse-submodules or run: git submodule update --init --recursive' }

# Fail before staging if tracked firmware source still contains merge-conflict markers.
$conflictMarkerMatches = @(git -C $repoRoot grep -n -E '^(<<<<<<<|=======|>>>>>>>)' -- '*.ino' '*.h' '*.c' '*.cpp')
$conflictMarkerSearchExit = $LASTEXITCODE
if ($conflictMarkerSearchExit -eq 0) {
    throw "Conflict markers found in firmware source:`n$($conflictMarkerMatches -join "`n")"
}
if ($conflictMarkerSearchExit -ne 1) {
    throw "Conflict-marker source scan failed with exit code $conflictMarkerSearchExit."
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

    Write-Host "Building Pico2Seq with $($arduinoCliCommand.Source)"
    Write-Host "Artifacts: $buildPath"
    Write-Host "CPU clock: $CpuMHz MHz"
    Write-Host "Audio code in RAM: $audioInRam"
    & $arduinoCliCommand.Source compile `
        --fqbn 'rp2040:rp2040:rpipico2' `
        --board-options $boardOptions `
        --warnings all `
        --clean `
        --build-property "build.extra_flags=-ffast-math -DPICO2SEQ_AUDIO_IN_RAM=$audioInRam" `
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

    # Rename/copy the UF2 from wherever this build landed: publish_uf2.ps1
    # finds the newest *.uf2 recursively, stages the canonical
    # "<title>_Pico2Seq_<date>.uf2" next to the original (the original stays
    # for `arduino-cli upload --input-dir`), and copies it to the working folder.
    $publishScript = Join-Path $PSScriptRoot 'publish_uf2.ps1'
    if (Test-Path -LiteralPath $publishScript -PathType Leaf) {
        & $publishScript -BuildDir $buildPath -FirmwareTitle $FirmwareTitle `
            -WorkingUf2Dir $WorkingUf2Dir -NoWorkingCopy:$NoWorkingCopy
    } else {
        Write-Warning 'publish_uf2.ps1 was not found next to this script; skipping UF2 rename/copy.'
    }

    $buildSucceeded = $true
    Write-Host 'Build completed successfully. Firmware was compiled only; it was not uploaded or hardware-tested.'
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
