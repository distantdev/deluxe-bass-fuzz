# Copy the DeluxeBassFuzz build to the DAW laptop over SMB.
#
#   .\tools\deploy_to_daw.ps1 [-TargetHost DAW-LAPTOP] [-BuildDir build-vs\DeluxeBassFuzz_artefacts\Release]
#
# Same pattern as the sibling Subdoom repo (tools\deploy_to_daw.ps1):
#   VST3       -> \\<host>\VST3                       (C:\Program Files\Common Files\VST3)
#   CLAP       -> \\<host>\CLAP                       (C:\Program Files\Common Files\CLAP)
#   Standalone -> \\<host>\DeluxeBassFuzzStandalone   (C:\Program Files\DeluxeBassFuzz)
# The shares are created on the laptop by tools\setup_daw_share.ps1 (VST3/CLAP by Doomgazer's).
#
# Differences from Subdoom's script:
#   - No fallback into the generic "Standalone" share: that share is Doomgazer's folder
#     (C:\Program Files\Doomgazer), and Subdoom's fallback left a mislocated exe there that
#     its setup script later had to clean up. If DeluxeBassFuzzStandalone is missing, the
#     standalone is skipped, VST3/CLAP are still deployed, and the script exits 2 with the
#     one-time laptop setup command.
#   - Refuses any artefact that is not named DeluxeBassFuzz* (so Subdoom/Doomgazer files can
#     never be copied from here).
#   - -SkipClap for a build configured with -DBMD_BUILD_CLAP=OFF.
param(
    [string]$TargetHost = "DAW-LAPTOP",
    [string]$BuildDir = "",
    [switch]$SkipClap
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build-vs\DeluxeBassFuzz_artefacts\Release"
} elseif (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $repoRoot $BuildDir
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

$pluginName = "Deluxe Bass Fuzz"
$standaloneShareName = "DeluxeBassFuzzStandalone"
$standaloneFolderOnLaptop = "C:\Program Files\DeluxeBassFuzz"

if (-not (Test-Path -LiteralPath $BuildDir)) {
    throw "Build directory not found: $BuildDir"
}

# Refuse to ship a build older than the sources: a failed build must not be
# silently deployed as if it were current.
$newestSource = Get-ChildItem -Path (Join-Path $repoRoot "Source") -Recurse -File -Include *.cpp, *.h |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($null -ne $newestSource) {
    $artefactFiles = Get-ChildItem -Path $BuildDir -Recurse -File `
        -Include *.vst3, *.clap, *.exe -ErrorAction SilentlyContinue
    $stale = $artefactFiles | Where-Object { $_.LastWriteTime -lt $newestSource.LastWriteTime }
    if ($stale) {
        $names = ($stale | ForEach-Object { $_.Name }) -join ", "
        Write-Error "Artefacts are older than $($newestSource.Name); build before deploying ($names)." -ErrorAction Stop
        exit 1
    }
}

function Get-CopiedBytes([string]$Path) {
    $item = Get-Item -LiteralPath $Path
    if (-not $item.PSIsContainer) {
        return [int64]$item.Length
    }
    $sum = Get-ChildItem -LiteralPath $Path -Recurse -File | Measure-Object -Property Length -Sum
    if ($null -eq $sum.Sum) {
        return [int64]0
    }
    return [int64]$sum.Sum
}

function Test-FileInUse([System.Management.Automation.ErrorRecord]$ErrorRecord) {
    $message = $ErrorRecord.Exception.Message
    if ($message -match "used by another process") {
        return $true
    }
    $current = $ErrorRecord.Exception
    while ($null -ne $current) {
        if ($current.HResult -eq -2147024864) {   # ERROR_SHARING_VIOLATION
            return $true
        }
        $current = $current.InnerException
    }
    return $false
}

function Copy-Required([string]$Label, [string]$Source, [string]$Share) {
    $problems = @()
    if (-not (Test-Path -LiteralPath $Share)) {
        $problems += "$Label share is missing: $Share"
    }
    if (-not (Test-Path -LiteralPath $Source)) {
        $problems += "$Label artefact is missing: $Source"
    }
    if ($problems.Count -gt 0) {
        throw ($problems -join [Environment]::NewLine)
    }

    $leaf = Split-Path -Leaf $Source
    if ($leaf -notlike "$pluginName*" -or $leaf -like "Subdoom*" -or $leaf -like "Doomgazer*") {
        throw "Refusing to copy $leaf. This script deploys $pluginName only."
    }
    $destination = Join-Path $Share $leaf
    try {
        Copy-Item -LiteralPath $Source -Destination $Share -Recurse -Force
    } catch {
        if (Test-FileInUse $_) {
            throw @"
$Label is in use: $destination
Unload $pluginName in Bitwig and REAPER, quit the laptop standalone, and rerun.
$($_.Exception.Message)
"@
        }
        throw "$Label copy failed: $Source -> $Share. $($_.Exception.Message)"
    }

    $copied = Get-Item -LiteralPath $destination
    $bytes = Get-CopiedBytes $destination
    $when = $copied.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")
    Write-Host ("  -> {0}  {1} bytes  {2}  {3}" -f $Label, $bytes, $when, $destination) -ForegroundColor Green
    return $destination
}

Write-Host ">>> Deploying DELUXEBASSFUZZ to $TargetHost from $BuildDir" -ForegroundColor Cyan

$vst3 = Join-Path $BuildDir "VST3\$pluginName.vst3"
$clap = Join-Path $BuildDir "CLAP\$pluginName.clap"
$standalone = Join-Path $BuildDir "Standalone\$pluginName.exe"

Copy-Required "VST3" $vst3 "\\$TargetHost\VST3" | Out-Null
if ($SkipClap) {
    Write-Host "  -> CLAP skipped (-SkipClap)." -ForegroundColor Yellow
} else {
    Copy-Required "CLAP" $clap "\\$TargetHost\CLAP" | Out-Null
}

$dedicated = "\\$TargetHost\$standaloneShareName"
$standaloneSkipped = $false
if (Test-Path -LiteralPath $dedicated) {
    Copy-Required "Standalone" $standalone $dedicated | Out-Null
    Write-Host "Laptop standalone: $standaloneFolderOnLaptop\$pluginName.exe"
} else {
    $standaloneSkipped = $true
    Write-Warning "No $standaloneShareName share on $TargetHost; the standalone was NOT deployed (no fallback into Doomgazer's Standalone share)."
    Write-Host "  One-time setup: copy tools\setup_daw_share.ps1 to the laptop and run it there (it asks for admin):" -ForegroundColor Yellow
    Write-Host "    powershell -NoProfile -ExecutionPolicy Bypass -File .\setup_daw_share.ps1" -ForegroundColor Yellow
    Write-Host "  Then rerun .\tools\deploy_to_daw.ps1 -TargetHost $TargetHost" -ForegroundColor Yellow
}
Write-Host "Local standalone: $standalone"
Write-Host "  -> Presets: none to sync (the plugin has no preset files)."

Write-Host ">>> Deployment pass finished. Rescan plugins in Bitwig and REAPER." -ForegroundColor Green
if ($standaloneSkipped) {
    exit 2
}
