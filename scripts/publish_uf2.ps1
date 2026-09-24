<#
.SYNOPSIS
Renames a built Pico2Seq UF2 to its canonical share name and copies it to the working UF2 folder.

.DESCRIPTION
Finds the newest *.uf2 anywhere under -BuildDir (searched recursively, so it
works no matter which folder or sketch name the firmware was built from),
stages a renamed duplicate next to the original, and copies it to
-WorkingUf2Dir.

Name rule: "<title>_Pico2Seq_<yyyy-MM-dd>.uf2". If <title> already contains
"Pico2Seq", the extra infix is skipped: "<title>_<yyyy-MM-dd>.uf2".

The original UF2 is always kept in place (arduino-cli upload --input-dir
expects the sketch-named file), and a same-stem .elf duplicate is staged
alongside when the build's .elf sits next to the UF2, so post-mortem
addr2line decoding stays matched to the shared file.

.EXAMPLE
.\scripts\publish_uf2.ps1 -BuildDir build\fasti2c-225 -FirmwareTitle fasti2c
.\scripts\publish_uf2.ps1 -BuildDir build_fw -FirmwareTitle 'my test'
#>
[CmdletBinding()]
param(
    # Any build output folder; searched recursively for *.uf2.
    [Parameter(Mandatory)]
    [string]$BuildDir,
    # Name entered by the user (first prompt of build.ps1). Sanitized for
    # filesystem use; defaults to 'Pico2Seq' when empty.
    [string]$FirmwareTitle = '',
    # Keep this default in sync with build_pico2seq.ps1 -WorkingUf2Dir.
    [string]$WorkingUf2Dir = 'Z:\Codezzz\workingUF2',
    [switch]$NoWorkingCopy
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$buildPath = [IO.Path]::GetFullPath($BuildDir)
if (-not (Test-Path -LiteralPath $buildPath -PathType Container)) {
    throw "Build directory was not found: '$buildPath'."
}

$allUf2s = @(
    Get-ChildItem -LiteralPath $buildPath -Recurse -File -Filter '*.uf2' |
        Sort-Object LastWriteTime -Descending
)
if ($allUf2s.Count -eq 0) {
    throw "No .uf2 file found anywhere under '$buildPath'."
}
$sourceUf2 = $null
$sketchUf2s = @($allUf2s | Where-Object { $_.Name -in @('Pico2Seq.ino.uf2', 'Pico2Seq.uf2') })
if ($sketchUf2s.Count -gt 0) {
    # Prefer the compiler's own output over renamed copies staged by an
    # earlier run into the same folder (they are newer, but stale).
    $sourceUf2 = $sketchUf2s[0]
} else {
    $sourceUf2 = $allUf2s[0]
    if ($allUf2s.Count -gt 1) {
        Write-Warning ("Found $($allUf2s.Count) .uf2 files and no sketch-named output; using the newest: '$($sourceUf2.FullName)'.")
    }
}

if ([string]::IsNullOrWhiteSpace($FirmwareTitle)) {
    $cleanTitle = 'Pico2Seq'
} else {
    $cleanTitle = ($FirmwareTitle.Trim() -replace '[\\/:*?"<>|\x00-\x1F]', '_' -replace '\s+', ' ').Trim(' ', '_', '-')
    if ([string]::IsNullOrWhiteSpace($cleanTitle)) {
        $cleanTitle = 'Pico2Seq'
    }
}

$dateStamp = Get-Date -Format 'yyyy-MM-dd'
if ($cleanTitle -match '(?i)Pico2Seq') {
    $canonicalStem = '{0}_{1}' -f $cleanTitle, $dateStamp
} else {
    $canonicalStem = '{0}_Pico2Seq_{1}' -f $cleanTitle, $dateStamp
}

$sourceDir = Split-Path -Parent $sourceUf2.FullName
$stagedUf2 = Join-Path $sourceDir ($canonicalStem + '.uf2')
Copy-Item -LiteralPath $sourceUf2.FullName -Destination $stagedUf2 -Force
Write-Host "Renamed UF2 staged: $stagedUf2"

$stagedElf = $null
$sourceElf = Join-Path $sourceDir ([IO.Path]::GetFileNameWithoutExtension($sourceUf2.Name) + '.elf')
if (Test-Path -LiteralPath $sourceElf -PathType Leaf) {
    $stagedElf = Join-Path $sourceDir ($canonicalStem + '.elf')
    Copy-Item -LiteralPath $sourceElf -Destination $stagedElf -Force
    Write-Host "Matching ELF staged: $stagedElf"
}

if (-not $NoWorkingCopy) {
    try {
        New-Item -ItemType Directory -Path $WorkingUf2Dir -Force | Out-Null
        Copy-Item -LiteralPath $stagedUf2 -Destination (Join-Path $WorkingUf2Dir ([IO.Path]::GetFileName($stagedUf2))) -Force
        if ($null -ne $stagedElf) {
            Copy-Item -LiteralPath $stagedElf -Destination (Join-Path $WorkingUf2Dir ([IO.Path]::GetFileName($stagedElf))) -Force
        }
        Write-Host "Copied to working folder: $WorkingUf2Dir"
    } catch {
        Write-Warning "Build artifacts are staged above, but copying to '$WorkingUf2Dir' failed: $($_.Exception.Message)"
    }
}

$stagedUf2
