[CmdletBinding()]
param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$BuildDirectory,
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
$stageRoot = Join-Path ([IO.Path]::GetTempPath()) "Pico2Seq-arduino-stage-$stamp"
$stageSketch = Join-Path $stageRoot 'Pico2Seq'
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $buildPath = Join-Path $repoRoot "build\arduino-cli\Pico2Seq-current-$stamp"
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
    'flash=4194304_0'
    'arch=arm'
    'freq=300'
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
try {
    Copy-StageTree -Source $repoRoot -Destination $stageSketch -IsRepositoryRoot $true
    New-Item -ItemType Directory -Path $buildPath -Force | Out-Null

    Write-Host "Building Pico2Seq with $($arduinoCliCommand.Source)"
    Write-Host "Artifacts: $buildPath"
    & $arduinoCliCommand.Source compile `
        --fqbn 'rp2040:rp2040:rpipico2' `
        --board-options $boardOptions `
        --warnings all `
        --clean `
        --build-property 'build.extra_flags=-ffast-math' `
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
