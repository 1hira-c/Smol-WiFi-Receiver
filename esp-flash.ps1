# SPDX-License-Identifier: MIT OR Apache-2.0
param(
    [string]$Port,
    [string]$Python = "C:\Espressif\tools\python\v6.0.1\venv\Scripts\python.exe"
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Python)) {
    throw "ESP-IDF Python was not found at $Python"
}
if (-not $Port) {
    $ports = @(Get-CimInstance Win32_SerialPort | Where-Object {
        $_.PNPDeviceID -match 'VID_303A'
    })
    if ($ports.Count -ne 1) {
        throw "Expected one Espressif USB serial port; pass -Port COMx after entering download mode"
    }
    $Port = $ports[0].DeviceID
}

$build = Join-Path $PSScriptRoot "firmware\esp32c5\build"
$bootloader = Join-Path $build "bootloader\bootloader.bin"
$partitions = Join-Path $build "partition_table\partition-table.bin"
$application = Join-Path $build "slimevr_smol_wifi_gateway.bin"
foreach ($file in @($bootloader, $partitions, $application)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing build output: $file"
    }
}

Write-Host "Flashing ESP32-C5 on $Port"
$arguments = @(
    "-m", "esptool", "--chip", "esp32c5", "--port", $Port, "--baud", "460800",
    "--before", "default-reset", "--after", "hard-reset", "write-flash",
    "--flash-mode", "dio", "--flash-size", "8MB", "--flash-freq", "80m",
    "0x2000", $bootloader,
    "0x8000", $partitions,
    "0x10000", $application
)
$process = Start-Process -FilePath $Python -ArgumentList $arguments `
    -NoNewWindow -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "esptool failed with exit code $($process.ExitCode)"
}

Write-Host "Flash complete. Open $Port at 115200 baud and look for 'SPI link established'."
