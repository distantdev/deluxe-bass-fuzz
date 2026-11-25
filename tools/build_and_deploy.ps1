# Build, test, pluginval and deploy DeluxeBassFuzz to the DAW laptop.
#
#   .\tools\build_and_deploy.ps1 [-TargetHost DAW-LAPTOP] [-Config Release] [-SkipBuild] [-SkipChecks]
#
# Same shape as Subdoom's tools\build_and_deploy.ps1, but the build goes through this repo's
# build.ps1 (build-vs, Visual Studio 18 2026, unit tests via ctest and pluginval strictness 5),
# so a build that fails its tests or pluginval is never deployed. Exit codes: 0 ok,
# 2 deployed without the standalone (laptop share missing), anything else = failure.
param(
    [string]$TargetHost = "DAW-LAPTOP",
    [ValidateSet("Release", "Debug")] [string]$Config = "Release",
    [string]$BuildDir = "build-vs",
    [switch]$SkipBuild,
    [switch]$SkipChecks,
    [switch]$SkipClap
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$buildPath = [System.IO.Path]::GetFullPath($buildPath)

$expectedGenerator = "Visual Studio 18 2026"

function Get-CMakeCacheValue([string]$CachePath, [string]$Key) {
    $pattern = "^" + [regex]::Escape($Key) + "=(.*)$"
    $match = Select-String -LiteralPath $CachePath -Pattern $pattern | Select-Object -First 1
    if ($null -eq $match) {
        return ""
    }
    return $match.Matches[0].Groups[1].Value.Trim()
}

if (-not $SkipBuild) {
    $cache = Join-Path $buildPath "CMakeCache.txt"
    if (Test-Path -LiteralPath $cache) {
        $generator = Get-CMakeCacheValue $cache "CMAKE_GENERATOR:INTERNAL"
        if ($generator -ne $expectedGenerator) {
            throw "CMake generator is '$generator', expected '$expectedGenerator'. Refusing to reconfigure $buildPath. Do not pass -G Ninja."
        }
        $clapOpt = Get-CMakeCacheValue $cache "BMD_BUILD_CLAP:BOOL"
        if ($clapOpt -eq "OFF" -and -not $SkipClap) {
            throw "BMD_BUILD_CLAP is OFF in $buildPath. Pass -SkipClap or reconfigure with -DBMD_BUILD_CLAP=ON."
        }
    }

    $buildArgs = @{ Config = $Config; BuildDir = $BuildDir }
    if ($SkipChecks) {
        $buildArgs.SkipTests = $true
        $buildArgs.SkipPluginval = $true
    }
    Write-Host ">>> build.ps1 ($Config$(if ($SkipChecks) { ', checks skipped' } else { ', unit tests + pluginval' }))" -ForegroundColor Cyan
    & (Join-Path $repoRoot "build.ps1") @buildArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build or checks failed (exit $LASTEXITCODE); not deploying." -ErrorAction Continue
        exit $LASTEXITCODE
    }
}

$artefacts = Join-Path $buildPath "DeluxeBassFuzz_artefacts\$Config"
& (Join-Path $PSScriptRoot "deploy_to_daw.ps1") -TargetHost $TargetHost -BuildDir $artefacts -SkipClap:$SkipClap
exit $LASTEXITCODE
