# Builds every TLEET plugin on Windows, runs the tests and installs the VST3s.
# Needs: Visual Studio 2022 Build Tools ("Desktop development with C++"),
# CMake 3.22+ and Git. Run from the repository root in PowerShell:
#   (in "Developer PowerShell for VS 2022", so cmake is on PATH)
#   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
# Add -Install to copy the plugins into C:\Program Files\Common Files\VST3
# (the window must be started "as administrator" for that).

param([switch]$Install)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)

cmake -B build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Push-Location build/tests
./tleet_tests_artefacts/Release/tleet_tests.exe
$testResult = $LASTEXITCODE
Pop-Location
if ($testResult -ne 0) { Write-Host "Tests failed, see build/tests/test-output" -ForegroundColor Red; exit $testResult }

$plugins = Get-ChildItem build/plugins -Recurse -Filter *.vst3 -Directory | Where-Object { $_.Parent.Name -eq 'VST3' }
foreach ($p in $plugins) { Write-Host "Built: $($p.FullName)" -ForegroundColor Green }

if ($Install) {
    $dest = "$env:CommonProgramFiles\VST3"
    foreach ($p in $plugins) {
        Copy-Item $p.FullName -Destination $dest -Recurse -Force
        Write-Host "Installed: $dest\$($p.Name)" -ForegroundColor Green
    }
}
