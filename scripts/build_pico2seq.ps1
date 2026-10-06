[CmdletBinding()]
param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$BuildDirectory,
    [ValidateSet(150, 225, 300)] [int]$CpuMHz = 225,
    [switch]$AudioInFlash,
    # Extra compiler flags appended to build.extra_flags, e.g. the reverb A/B builds:
    #   -ExtraFlags '-DPICO2SEQ_REVERB_STORAGE_HALF=0'  (Float tank; default is Half)
    #   -ExtraFlags '-DPICO2SEQ_REVERB_BYPASS=1'        (bench baseline, no reverb)
    [string]$ExtraFlags = '',
    [switch]$KeepStage
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
$stageName = "Pico2Seq-arduino-stage-$stamp-$([Guid]::NewGuid().ToString('N'))"
$stageRoot = Join-Path ([IO.Path]::GetTempPath()) $stageName
$stageSketch = Join-Path $stageRoot 'Pico2Seq'
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $buildPath = Join-Path (Join-Path (Join-Path $repoRoot 'build') 'arduino-cli') "Pico2Seq-current-$stamp"
} else {
    $buildPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
}

$subStatus = git -C $repoRoot submodule status --recursive
if ($LASTEXITCODE -ne 0) {
    throw "Submodule status failed with exit code $LASTEXITCODE."
}
# git prefixes each line with ' ' (in sync), '+' (different commit checked out),
# '-' (not initialized) or 'U' (merge conflicts). Only the last three are stale.
if ($subStatus -match '^[\+\-U]') {
    throw "Submodules are not initialized or do not match the recorded pins. Run: git submodule update --init --recursive`n$subStatus`nThis helper does not reset, clean, or discard local work."
}

# Check both required submodules by their source entry points. A clean status line
# alone is not enough on a partially materialized checkout, and it says nothing about
# a pin that is in sync but too old: dark_reverb.h is the newest rpdsp header the
# firmware includes (MasterReverb.h), so a pin that predates the reverb fails here
# with a clear message instead of deep inside the compiler.
$requiredSubmoduleFiles = @(
    'src/rpdsp/src/rpdsp/DSPFunctions.h',
    'src/rpdsp/src/rpdsp/dark_reverb.h',
    'src/VelocityEncoder/src/MagEncoder.h'
)
$missingSubmoduleFiles = @($requiredSubmoduleFiles | Where-Object {
    -not (Test-Path (Join-Path $repoRoot $_) -PathType Leaf)
})
if ($missingSubmoduleFiles.Count -gt 0) {
    throw "Required submodule source is missing. Run: git submodule update --init --recursive`nMissing: $($missingSubmoduleFiles -join ', ')`nThis helper does not reset, clean, or discard local work."
}

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
        [Parameter(Mandatory)] [string]$Destination
    )

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        # Arduino CLI recursively compiles sketch/src. Skip dependency examples
        # and Git metadata (including submodule .git files) at every depth.
        if ($item.Name -in @('.git', 'examples')) {
            continue
        }

        if ($item.PSIsContainer) {
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
    New-Item -ItemType Directory -Path $stageSketch -Force | Out-Null
    foreach ($name in @('Pico2Seq.ino', 'diagnostic.h')) {
        Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $stageSketch -Force
    }
    Copy-StageTree -Source (Join-Path $repoRoot 'src') -Destination (Join-Path $stageSketch 'src')
    New-Item -ItemType Directory -Path $buildPath -Force | Out-Null

    Write-Host "Building Pico2Seq with $($arduinoCliCommand.Source)"
    Write-Host "Artifacts: $buildPath"
    Write-Host "CPU clock: $CpuMHz MHz"
    Write-Host "Audio code in RAM: $audioInRam"
    if (-not [string]::IsNullOrWhiteSpace($ExtraFlags)) {
        Write-Host "Extra flags: $ExtraFlags"
    }
    & $arduinoCliCommand.Source compile `
        --fqbn 'rp2040:rp2040:rpipico2' `
        --board-options $boardOptions `
        --warnings all `
        --clean `
        --build-property "build.extra_flags=-ffast-math -DPICO2SEQ_AUDIO_IN_RAM=$audioInRam $ExtraFlags" `
        --build-path $buildPath `
        $stageSketch

    if ($LASTEXITCODE -ne 0) {
        throw "Arduino CLI compilation failed with exit code $LASTEXITCODE."
    }

    $artifacts = @('.uf2', '.elf', '.bin', '.map')
    $missingArtifacts = @($artifacts | Where-Object {
        $artifact = Join-Path $buildPath "Pico2Seq.ino$PSItem"
        -not (Test-Path -LiteralPath $artifact -PathType Leaf) -or
            (Get-Item -LiteralPath $artifact).Length -eq 0
    })
    if ($missingArtifacts.Count -gt 0) {
        throw "Arduino CLI exited with code 0, but these expected artifacts were not found: $($missingArtifacts -join ', ')."
    }

    $buildSucceeded = $true
    Write-Host 'Build completed successfully. Firmware was compiled only; it was not uploaded or hardware-tested.'
} finally {
    if (-not $KeepStage) {
        if (Test-Path -LiteralPath $stageRoot) {
            $resolvedStage = (Resolve-Path -LiteralPath $stageRoot).Path
            $resolvedTemp = (Resolve-Path -LiteralPath ([IO.Path]::GetTempPath())).Path
            if ($resolvedStage -ne (Join-Path $resolvedTemp $stageName)) {
                throw "Refusing to remove a stage outside the expected temporary directory: $resolvedStage"
            }
            Remove-Item -LiteralPath $stageRoot -Recurse -Force
        }
    } elseif ($buildSucceeded) {
        Write-Host "Staging directory retained: $stageRoot"
    } else {
        Write-Warning "Build failed; staging directory retained for diagnosis: $stageRoot"
    }
}
