param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [ValidateSet('default', 'no_ota', 'huge_app', 'min_spiffs')]
    [string]$Partition = 'default',

    [int]$Baud = 460800,

    [string]$DataDir = "$(Join-Path $PSScriptRoot '..\data')",

    [string]$Chip = 'esp32'
)

$ErrorActionPreference = 'Stop'

function Get-LatestToolExe {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ToolName,
        [Parameter(Mandatory = $true)]
        [string]$ExeName
    )

    $toolRoot = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\tools\$ToolName"
    if(-not (Test-Path $toolRoot)) {
        throw "Tool root not found: $toolRoot"
    }

    $candidates = Get-ChildItem -Path $toolRoot -Directory | Sort-Object Name -Descending
    foreach($dir in $candidates) {
        $exePath = Join-Path $dir.FullName $ExeName
        if(Test-Path $exePath) {
            return $exePath
        }
    }

    throw "Executable $ExeName not found under $toolRoot"
}

if(-not (Test-Path $DataDir)) {
    throw "Data directory not found: $DataDir"
}

$partitionTable = @{
    default    = @{ Offset = '0x290000'; Size = '0x160000' }
    no_ota     = @{ Offset = '0x210000'; Size = '0x1E0000' }
    huge_app   = @{ Offset = '0x310000'; Size = '0x0E0000' }
    min_spiffs = @{ Offset = '0x3D0000'; Size = '0x020000' }
}

$entry = $partitionTable[$Partition]
$offsetHex = $entry.Offset
$sizeHex = $entry.Size
$sizeBytes = [Convert]::ToInt32($sizeHex, 16)

$mkspiffs = Get-LatestToolExe -ToolName 'mkspiffs' -ExeName 'mkspiffs.exe'
$esptool = Get-LatestToolExe -ToolName 'esptool_py' -ExeName 'esptool.exe'

$buildDir = Join-Path $PSScriptRoot '..\build'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
$imagePath = Join-Path $buildDir 'spiffs.bin'

Write-Host "[SPIFFS] Data dir    : $DataDir"
Write-Host "[SPIFFS] Partition   : $Partition"
Write-Host "[SPIFFS] Offset      : $offsetHex"
Write-Host "[SPIFFS] Size        : $sizeHex ($sizeBytes bytes)"
Write-Host "[SPIFFS] mkspiffs    : $mkspiffs"
Write-Host "[SPIFFS] esptool     : $esptool"

Get-ChildItem -Path $DataDir -File | Select-Object Name, Length | Format-Table -AutoSize

& $mkspiffs -c $DataDir -b 4096 -p 256 -s $sizeBytes $imagePath

if(-not (Test-Path $imagePath)) {
    throw "Image not generated: $imagePath"
}

$imageSize = (Get-Item $imagePath).Length
Write-Host "[SPIFFS] Image built : $imagePath ($imageSize bytes)"

& $esptool --chip $Chip --port $Port --baud $Baud write-flash $offsetHex $imagePath

Write-Host "[SPIFFS] Upload complete"
