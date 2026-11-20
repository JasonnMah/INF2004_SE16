# Build Sensor firmware on Windows; this script does not flash a connected Pico.
# From the repository root:
#   .\wifi_pico\tests\sensor_live_logger\build.ps1
# Add -I2CDiagnostics to include the saved startup I2C trace.
param(
    [string]$PicoSdkPath = "$env:USERPROFILE\.pico-sdk\sdk\2.2.0",
    [switch]$I2CDiagnostics
)
$ErrorActionPreference='Stop'
# Explicit default avoids an old PICO_SDK_PATH from another terminal/project.
# Check exact dependency versions so another installed SDK is not used by mistake.
$sdkCommit=& git -C $PicoSdkPath rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $sdkCommit -ne 'a1438dff1d38bd9c65dbd693f0e5db4b9ae91779') { throw 'Use pinned Pico SDK 2.2.0.' }
$usbCommit=& git -C (Join-Path $PicoSdkPath 'lib/tinyusb') rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $usbCommit -ne '86ad6e56c1700e85f1c5678607a762cfe3aa2f47') { throw 'Wrong pinned TinyUSB dependency.' }
# Use the Pico tools installed under the current Windows user's profile.
$tools=Join-Path $env:USERPROFILE '.pico-sdk'
$cmake=Join-Path $tools 'cmake\v3.31.5\bin\cmake.exe'
$ninja=Join-Path $tools 'ninja\v1.12.1\ninja.exe'
$toolchain=Join-Path $tools 'toolchain\14_2_Rel1'
$build=Join-Path $PSScriptRoot 'build'
# Give the diagnostic variant its own filename to avoid confusing the two UF2s.
$target=if($I2CDiagnostics) { 'sensor_live_logger_i2c_diag' } else { 'sensor_live_logger' }
$savedPath=$env:PATH
try {
    $env:PATH=(Join-Path $toolchain 'bin')+';'+$env:PATH
    # Configure a Pico W build, then compile only the selected Sensor target.
    & $cmake -S $PSScriptRoot -B $build -G Ninja '-DPICO_BOARD=pico_w' "-DPICO_SDK_PATH=$PicoSdkPath" `
        '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON' `
        "-DCMAKE_MAKE_PROGRAM=$ninja" "-DPICO_TOOLCHAIN_PATH=$toolchain" `
        "-Dpioasm_DIR=$tools\tools\2.2.0\pioasm" "-Dpicotool_DIR=$tools\picotool\2.2.0-a4\picotool"
    if($LASTEXITCODE -ne 0) { throw 'Live Sensor configuration failed.' }
    & $cmake --build $build --target $target --parallel 4
    if($LASTEXITCODE -ne 0) { throw 'Live Sensor build failed.' }
# Restore this terminal's PATH even if configuration or compilation fails.
} finally { $env:PATH=$savedPath }
Write-Output "UF2: $(Join-Path $build "$target.uf2")"
Write-Output 'Build only. Real AHT10/UART/HTTP testing is performed by the user. RAM only; flash and RTC are not integrated.'
Write-Output "Removable: this entire folder, including build: $PSScriptRoot"
