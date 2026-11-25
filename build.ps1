# Build, test, and validate DeluxeBassFuzz on Windows.
# Usage: .\build.ps1 [-Config Release|Debug] [-Clean] [-SkipTests] [-SkipPluginval] [-Validate] [-RerunSpice]
#   -Validate    SPICE-vs-DSP comparison: render the DSP blocks, compare with the
#                ngspice reference data, write docs/validation/*.png + docs/validation.md,
#                and run the 'validation' ctest label.
#   -RerunSpice  with -Validate: regenerate tests/ReferenceData/spice with ngspice first
#                (needs ngspice_con.exe; default %USERPROFILE%\tools\ngspice\Spice64\bin).
#   -Deploy      if everything above passed, copy VST3/CLAP/Standalone to the DAW laptop
#                with tools\deploy_to_daw.ps1 (-TargetHost, default DAW-LAPTOP).
#                tools\build_and_deploy.ps1 is the one-command equivalent.
param(
    [ValidateSet("Release", "Debug")] [string]$Config = "Release",
    [switch]$Clean,
    [switch]$SkipTests,
    [switch]$SkipPluginval,
    [switch]$Validate,
    [switch]$RerunSpice,
    [switch]$Deploy,
    [string]$TargetHost = "DAW-LAPTOP",
    [string]$BuildDir = "build-vs",
    [string]$Pluginval = "$env:USERPROFILE\tools\pluginval\pluginval.exe",
    [string]$Ngspice = "$env:USERPROFILE\tools\ngspice\Spice64\bin\ngspice_con.exe"
)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Test-Path "JUCE\CMakeLists.txt")) {
    git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "git submodule update failed" }
}

if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }

if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    cmake -S . -B $BuildDir -G "Visual Studio 18 2026" -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$failed = $false
if (-not $SkipTests) {
    # Unit tests only; the SPICE comparison has its own label (see -Validate)
    ctest --test-dir $BuildDir -C $Config -LE validation --output-on-failure
    if ($LASTEXITCODE -ne 0) { Write-Warning "Some unit tests failed"; $failed = $true }
}

$vst3 = Join-Path $PSScriptRoot "$BuildDir\DeluxeBassFuzz_artefacts\$Config\VST3\Bass Fuzz Deluxe.vst3"
if (-not $SkipPluginval) {
    if (Test-Path $Pluginval) {
        # pluginval is a GUI-subsystem exe; piping makes PowerShell wait and set $LASTEXITCODE
        & $Pluginval --strictness-level 5 --validate-in-process --validate $vst3 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) { Write-Warning "pluginval reported failures"; $failed = $true }
    } else {
        Write-Warning "pluginval not found at $Pluginval; skipping validation"
    }
}

if ($Validate) {
    if ($RerunSpice) {
        python tools\run_spice.py --ngspice $Ngspice
        if ($LASTEXITCODE -ne 0) { throw "SPICE run failed" }
    }
    & "$BuildDir\DeluxeBassFuzzRender_artefacts\$Config\DeluxeBassFuzzRender.exe" --root $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { throw "DSP render failed" }
    python tools\compare_spice.py
    if ($LASTEXITCODE -ne 0) { throw "compare_spice.py failed" }
    ctest --test-dir $BuildDir -C $Config -L validation --output-on-failure
    if ($LASTEXITCODE -ne 0) { Write-Warning "Some SPICE-vs-DSP checks failed (see docs/validation.md)"; $failed = $true }
}

Write-Host "VST3:       $vst3"
Write-Host "Standalone: $(Join-Path $PSScriptRoot "$BuildDir\DeluxeBassFuzz_artefacts\$Config\Standalone\Bass Fuzz Deluxe.exe")"
if ($failed) {
    if ($Deploy) { Write-Warning "Not deploying: tests, pluginval or validation failed." }
    exit 1
}
if ($Deploy) {
    & (Join-Path $PSScriptRoot "tools\deploy_to_daw.ps1") -TargetHost $TargetHost `
        -BuildDir (Join-Path $PSScriptRoot "$BuildDir\DeluxeBassFuzz_artefacts\$Config")
    exit $LASTEXITCODE
}
