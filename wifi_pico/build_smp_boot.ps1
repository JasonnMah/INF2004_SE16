param(
    [switch]$Clean,
    [string]$PicoSdkPath = $(if ($env:PICO_SDK_PATH) { $env:PICO_SDK_PATH } else { "$env:USERPROFILE\.pico-sdk\sdk\2.2.0" }),
    [string]$Distribution = "Ubuntu-24.04"
)
$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
. (Join-Path $projectRoot "prepare_course_build.ps1")
Initialize-CourseBuild -CourseRoot (Join-Path $projectRoot "course")
if (-not (Test-Path -LiteralPath (Join-Path $PicoSdkPath "lib\tinyusb\src\tusb.c"))) {
    throw "Pico SDK 2.2.0 with TinyUSB is required: $PicoSdkPath"
}
# Pin the external USB dependency as well as the vendored course revision.
$sdkCommit = & git -C $PicoSdkPath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $sdkCommit -ne "a1438dff1d38bd9c65dbd693f0e5db4b9ae91779") {
    throw "This milestone is pinned to Pico SDK 2.2.0 commit a1438dff1d38bd9c65dbd693f0e5db4b9ae91779."
}
$usbCommit = & git -C (Join-Path $PicoSdkPath "lib\tinyusb") rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $usbCommit -ne "86ad6e56c1700e85f1c5678607a762cfe3aa2f47") {
    throw "TinyUSB does not match the SDK 2.2.0 submodule revision."
}
$wslProject = (& wsl -d $Distribution -- wslpath -a ($projectRoot.Replace('\', '/'))).Trim()
if ($LASTEXITCODE -ne 0) { throw "Cannot resolve project path in WSL." }
$wslSdk = (& wsl -d $Distribution -- wslpath -a ($PicoSdkPath.Replace('\', '/'))).Trim()
if ($LASTEXITCODE -ne 0) { throw "Cannot resolve SDK path in WSL." }
$buildPath = Join-Path $projectRoot "course\build_make"
$arguments = @("-d", $Distribution, "--", "env", "PICO_SDK_PATH=$wslSdk",
    "make", "-C", "$wslProject/course/build_make", "-f", "makefile",
    "INF2004_MILESTONE1=1", "PROFILE_ID=inf2004-milestone1-smp1-usb-only-v1", "SMP=1", "CONSOLE=usb_cdc", "WIFI=none")
if ($Clean) {
    & wsl @arguments clean
    if ($LASTEXITCODE -ne 0) { throw "Clean failed." }
    # Upstream clean omits UF2; avoid leaving a stale success artifact.
    $oldUf2 = Join-Path $buildPath "wifi_pico_smp_boot.uf2"
    if (Test-Path -LiteralPath $oldUf2) { Remove-Item -LiteralPath $oldUf2 }
}
$logPath = Join-Path $buildPath "milestone1-build.log"
& wsl @arguments milestone1-config all -j4 2>&1 | Tee-Object -FilePath $logPath
if ($LASTEXITCODE -ne 0) { throw "Build failed; see $logPath" }
if ($Clean) {
    & wsl -d $Distribution -- python3 "$wslProject/tests/verify_milestone1.py"
    if ($LASTEXITCODE -ne 0) { throw "Build/source verification failed." }
} else {
    Write-Output "Use -Clean to refresh the recorded compiler-invocation checks."
}
Write-Output "UF2: $(Join-Path $buildPath 'wifi_pico_smp_boot.uf2')"
Write-Output "Build only. Physical SMP operation requires a captured USB transcript."
