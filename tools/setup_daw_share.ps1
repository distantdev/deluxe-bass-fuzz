# Run this on the DAW laptop, elevated. It gives DeluxeBassFuzz its own standalone folder
# and the share that tools/deploy_to_daw.ps1 copies the standalone into.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\setup_daw_share.ps1
#
# The VST3 and CLAP shares already exist on the laptop (Doomgazer's setup made them).
# This script never repoints or removes any share; it only adds DeluxeBassFuzzStandalone
# (-> C:\Program Files\DeluxeBassFuzz) when it is missing, mirroring Subdoom's script.
param(
    [string]$Account = "",
    [string]$ComputerName = "DAW-LAPTOP"
)

$ErrorActionPreference = "Stop"

function Test-IsAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if ([string]::IsNullOrWhiteSpace($Account)) {
    # The exact identity, not "$env:USERDOMAIN\$env:USERNAME": a Microsoft
    # account has no meaningful USERDOMAIN, and icacls then fails to resolve it.
    $Account = [Security.Principal.WindowsIdentity]::GetCurrent().Name
}

if (-not (Test-IsAdmin)) {
    $hostExe = (Get-Command pwsh -ErrorAction SilentlyContinue).Source
    if (-not $hostExe) {
        $hostExe = (Get-Command powershell -ErrorAction SilentlyContinue).Source
    }
    # One quoted string, not an array: Start-Process joins an array with spaces
    # and never quotes the elements, so a path with a space breaks the elevated run.
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Account `"$Account`" -ComputerName `"$ComputerName`""
    Write-Host "Requesting administrator approval..." -ForegroundColor Cyan
    try {
        $proc = Start-Process -FilePath $hostExe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    } catch {
        throw "Elevation was refused or failed, so nothing was changed. Run this from an already elevated PowerShell window instead: $($_.Exception.Message)"
    }
    exit $proc.ExitCode
}

if ($env:COMPUTERNAME -ne $ComputerName) {
    Write-Warning "This machine is $env:COMPUTERNAME, not $ComputerName. The deploy script looks up \\$ComputerName, so run this on the laptop."
}

function Ensure-Folder([string]$Path) {
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Grant-Modify([string]$Path, [string]$Who) {
    & icacls $Path /grant "${Who}:(OI)(CI)M" /T /C | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "icacls failed for $Path ($Who), exit $LASTEXITCODE"
    }
}

$standaloneFolder = "C:\Program Files\DeluxeBassFuzz"
$shareName = "DeluxeBassFuzzStandalone"

Write-Host ">>> DeluxeBassFuzz DAW share setup" -ForegroundColor Cyan
Write-Host "    Account: $Account"

Ensure-Folder $standaloneFolder
Grant-Modify $standaloneFolder $Account
Grant-Modify $standaloneFolder "Administrators"

$existing = Get-SmbShare -Name $shareName -ErrorAction SilentlyContinue
if ($null -eq $existing) {
    New-SmbShare -Name $shareName -Path $standaloneFolder -ChangeAccess $Account -FullAccess "Administrators" | Out-Null
    Write-Host "  -> Share $shareName -> $standaloneFolder" -ForegroundColor Green
} elseif ($existing.Path -ne $standaloneFolder) {
    throw "Share $shareName already exists but points at '$($existing.Path)', not '$standaloneFolder'. Fix it by hand; this script does not repoint shares."
} else {
    Grant-SmbShareAccess -Name $shareName -AccountName $Account -AccessRight Change -Force | Out-Null
    Write-Host "  -> Share $shareName already points at $standaloneFolder" -ForegroundColor Green
}

foreach ($needed in @("VST3", "CLAP")) {
    if ($null -eq (Get-SmbShare -Name $needed -ErrorAction SilentlyContinue)) {
        Write-Warning "Share $needed is missing; run Doomgazer's tools\setup_daw_share.ps1 to create it."
    }
}

Write-Host ""
Write-Host "From the desktop, deploy again:" -ForegroundColor Cyan
Write-Host "  .\tools\deploy_to_daw.ps1 -TargetHost $ComputerName"
