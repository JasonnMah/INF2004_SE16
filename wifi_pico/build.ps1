param(
    [ValidateSet('uart','loopback','http','combined','boot')][string]$Target = 'uart',
    [switch]$Clean,
    [ValidateRange(0,3)][int]$Fault = 0,
    [switch]$VerboseDiagnostics,
    [switch]$BuildCheck,
    [string]$PicoSdkPath = $(if ($env:PICO_SDK_PATH) { $env:PICO_SDK_PATH } else { "$env:USERPROFILE\.pico-sdk\sdk\2.2.0" }),
    [string]$Distribution = 'Ubuntu-24.04'
)
$ErrorActionPreference = 'Stop'
if ($Target -notin @('uart','loopback') -and ($Fault -ne 0 -or $VerboseDiagnostics)) {
    throw '-Fault and -VerboseDiagnostics apply only to uart/loopback.'
}
if ($BuildCheck -and $Target -notin @('http','combined')) { throw '-BuildCheck applies only to http.' }
if ($Target -eq 'uart' -and $Fault -eq 1) { throw 'Use the disconnected-wire test for two-board command timeout.' }

# One pinned dependency check for every target. The course owns startup/IRQs.
$pins = @{
    '.' = 'a1438dff1d38bd9c65dbd693f0e5db4b9ae91779'
    'lib/tinyusb' = '86ad6e56c1700e85f1c5678607a762cfe3aa2f47'
}
if ($Target -in @('http','combined')) {
    $pins['lib/cyw43-driver'] = 'dd7568229f3bf7a37737b9e1ef250c26efe75b23'
    $pins['lib/lwip'] = '77dcd25a72509eb83f72b033d219b1d40cd8eb95'
}
foreach ($dependency in $pins.Keys) {
    $commit = & git -C (Join-Path $PicoSdkPath $dependency) rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $commit -ne $pins[$dependency]) { throw "Wrong pinned SDK dependency: $dependency" }
}

$course = Join-Path $PSScriptRoot 'course'
$rules = (Resolve-Path -LiteralPath (Join-Path $course 'build_rules')).Path
$build = [IO.Path]::GetFullPath((Join-Path $course 'build_make'))
New-Item -ItemType Directory -Path $build -Force | Out-Null
# Preserve the vendored course tree; only its generated workspace receives copies.
foreach ($file in Get-ChildItem -LiteralPath $rules -File -Recurse) {
    $relative = $file.FullName.Substring($rules.Length + 1)
    $destination = [IO.Path]::GetFullPath((Join-Path $build $relative))
    if (-not $destination.StartsWith($build + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Recipe destination escaped the build workspace.'
    }
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
}
$project = (& wsl -d $Distribution -- wslpath -a ($PSScriptRoot.Replace('\','/'))).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve project in WSL.' }
$sdk = (& wsl -d $Distribution -- wslpath -a ($PicoSdkPath.Replace('\','/'))).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve SDK in WSL.' }
$makeArgs = @('-d',$Distribution,'--','env',"PICO_SDK_PATH=$sdk",'make','-C',"$project/course/build_make",'-f','makefile',
    'SMP=1','CONSOLE=usb_cdc')
$diagnostics = [int][bool]$VerboseDiagnostics
$check = [int][bool]$BuildCheck
$verifyArgs = @()
switch ($Target) {
    'boot' {
        $image = 'wifi_pico_smp_boot'; $logName = 'milestone1-build.log'; $goal = 'milestone1-config'
        $makeArgs += @('INF2004_MILESTONE1=1','PROFILE_ID=inf2004-milestone1-smp1-usb-only-v2','WIFI=none')
        $verifier = 'verify_milestone1.py'
    }
    { $_ -in @('uart','loopback') } {
        $emulator = [int]($Target -eq 'loopback')
        $profile = if ($emulator) { "inf2004-uart-smp1-fault$Fault-diag$diagnostics-shared-v4" } else { "inf2004-uart-smp1-two-board-fault$Fault-diag$diagnostics-shared-v4" }
        $image = if ($emulator) { "wifi_pico_uart_loopback_f$Fault" } elseif ($Fault) { "wifi_pico_uart_two_board_f$Fault" } else { 'wifi_pico_uart_two_board' }
        $label = if ($emulator) { "f$Fault" } elseif ($Fault) { "two-board-f$Fault" } else { 'two-board' }
        if ($VerboseDiagnostics) { $image += '_verbose'; $label += '-verbose' }
        $logName = "uart-build-$label.log"; $goal = 'uart-config'
        $makeArgs += @('INF2004_UART=1',"PROFILE_ID=$profile", "APP_SENSOR_EMULATOR=$emulator", "UART_FAULT=$Fault", "APP_VERBOSE_DIAGNOSTICS=$diagnostics", 'WIFI=none')
        $verifier = 'verify_uart_build.py'; $verifyArgs = @("$Fault","$emulator","$diagnostics")
    }
    { $_ -in @('http','combined') } {
        $config = Join-Path $PSScriptRoot 'config/http_test.local.h'
        if ($BuildCheck) { $config = Join-Path $PSScriptRoot 'config/http_test.example.h' }
        if (-not (Test-Path -LiteralPath $config)) {
            throw 'Copy config/http_test.example.h to http_test.local.h, edit it and set HTTP_TEST_CONFIG_SET=1. Or use -BuildCheck for a disabled placeholder image.'
        }
        $generated = Join-Path $build 'http_test_config.h'
        # Avoid rebuilding driver objects when unchanged private settings are reused.
        if (-not (Test-Path -LiteralPath $generated) -or [IO.File]::ReadAllText($config) -ne [IO.File]::ReadAllText($generated)) {
            Copy-Item -LiteralPath $config -Destination $generated -Force
        }
        $image = 'wifi_pico_http_known_bytes'; if ($BuildCheck) { $image += '_buildcheck' }
        $logName = 'http-build.log'; $goal = 'http-config'
        $makeArgs += @('INF2004_HTTP=1',"PROFILE_ID=inf2004-http-known-smp1-check$check-v2", "HTTP_BUILD_CHECK=$check",
            'WIFI=cyw43','WIFI_JOIN=1','WIFI_NETIF=1','WIFI_DHCP=1',
            'WIFI_STATIC=0','WIFI_DNS=0','WIFI_UDP=0','WIFI_TCP=0','WIFI_TCPBULK=0')
        $verifier = 'verify_http_build.py'; $verifyArgs = @("$check")
        if ($Target -eq 'combined') {
            $image = 'wifi_pico_combined'; if ($BuildCheck) { $image += '_buildcheck' }
            $makeArgs = @($makeArgs | Where-Object { $_ -notlike 'PROFILE_ID=*' })
            $makeArgs += @('APP_COMBINED=1',"PROFILE_ID=inf2004-combined-smp1-check$check-v1")
            $logName = 'combined-build.log'; $verifyArgs += 'combined'
        }
    }
}
if ($Clean) {
    & wsl @makeArgs clean
    if ($LASTEXITCODE -ne 0) { throw 'Clean failed.' }
    $oldUf2 = Join-Path $build "$image.uf2"
    if (Test-Path -LiteralPath $oldUf2) { Remove-Item -LiteralPath $oldUf2 }
}
$logPath = Join-Path $build $logName
& wsl @makeArgs $goal all -j4 2>&1 | Tee-Object -FilePath $logPath
if ($LASTEXITCODE -ne 0) { throw "Build failed; see $logPath" }
if ($Clean -or $Target -in @('http','combined')) {
    & wsl -d $Distribution -- python3 "$project/tests/$verifier" @verifyArgs
    if ($LASTEXITCODE -ne 0) { throw 'Build/source verification failed.' }
} else { Write-Output 'Use -Clean to refresh actual compiler-invocation checks.' }
Write-Output "UF2: $(Join-Path $build "$image.uf2")"
if ($BuildCheck) { Write-Output 'BUILD CHECK ONLY: POST disabled. Configure real local settings and rebuild before flashing.' }
Write-Output 'Build/static checks only. Physical results require a separate user test record.'
