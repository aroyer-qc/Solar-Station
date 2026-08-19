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

Write-Host "[WARN] tools/upload_littlefs.ps1 is deprecated. Redirecting to SPIFFS uploader..."

$spiffsScript = Join-Path $PSScriptRoot 'upload_spiffs.ps1'
if(-not (Test-Path $spiffsScript)) {
    throw "SPIFFS uploader not found: $spiffsScript"
}

& $spiffsScript @PSBoundParameters
