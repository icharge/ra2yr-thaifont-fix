[CmdletBinding()]
param(
    [switch]$Restore,
    [string]$GameDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$defaultGameDirectory = Split-Path -Parent $projectRoot
$root = if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
    $defaultGameDirectory
} else {
    (Resolve-Path -LiteralPath $GameDirectory).Path
}
$integratedDll = Join-Path $PSScriptRoot 'ddraw-integrated.dll'
$fontConfig = Join-Path $projectRoot 'thaifontfix.ini'
$activeDll = Join-Path $root 'ddraw.dll'
$originalDll = Join-Path $root 'ddraw-custom.dll'
$integratedFileDescription = 'TS-DDraw with Thai bitmap-font mark placement fix'
$integratedFileVersion = '1.1.4.15'

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Test-IsIntegratedThaiFontFix([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $versionInfo = (Get-Item -LiteralPath $Path).VersionInfo
    return $versionInfo.FileDescription -eq $integratedFileDescription -and
        $versionInfo.FileVersion -eq $integratedFileVersion
}

$gameProcesses = @(Get-Process -Name @('RA2MD', 'GameMD', 'gamemdpatch', 'game') -ErrorAction SilentlyContinue)
if ($gameProcesses.Count -gt 0) {
    $names = ($gameProcesses | Select-Object -ExpandProperty ProcessName -Unique) -join ', '
    throw "Close the game before changing its renderer DLL. Still running: $names"
}

if (-not (Test-Path -LiteralPath $activeDll)) { throw "Game-folder ddraw.dll not found: $activeDll" }
if (-not (Test-Path -LiteralPath $fontConfig -PathType Leaf)) { throw "Shared ThaiFontFix configuration template is missing: $fontConfig" }
if (-not (Test-Path -LiteralPath $originalDll)) {
    Copy-Item -LiteralPath $activeDll -Destination $originalDll
    Write-Host "Saved the current renderer as $originalDll"
}
$originalHash = Get-Sha256 $originalDll
$currentHash = Get-Sha256 $activeDll

if ($Restore) {
    if ($currentHash -eq $originalHash) {
        Write-Host 'The original TS-DDraw is already active.'
        exit 0
    }
    if (-not (Test-Path -LiteralPath $integratedDll)) { throw "The staged integrated DLL is missing: $integratedDll" }
    $integratedHash = Get-Sha256 $integratedDll
    if ($currentHash -ne $integratedHash -and -not (Test-IsIntegratedThaiFontFix $activeDll)) {
        throw 'The active ddraw.dll is not the original backup or the staged integrated build; refusing to overwrite it.'
    }
    Copy-Item -LiteralPath $originalDll -Destination $activeDll -Force
    if ((Get-Sha256 $activeDll) -ne $originalHash) { throw 'Original renderer restore hash verification failed.' }
    Write-Host "Restored the original renderer from $originalDll"
    exit 0
}

if (-not (Test-Path -LiteralPath $integratedDll)) { throw "Build the integrated DLL first: $integratedDll" }
$integratedHash = Get-Sha256 $integratedDll
if ($currentHash -ne $originalHash -and $currentHash -ne $integratedHash -and
    -not (Test-IsIntegratedThaiFontFix $activeDll)) {
    throw 'The active ddraw.dll is not the original backup or the current integrated build; refusing to overwrite it.'
}

Copy-Item -LiteralPath $integratedDll -Destination $activeDll -Force
if ((Get-Sha256 $activeDll) -ne $integratedHash) { throw 'Integrated renderer install hash verification failed.' }
Write-Host "Installed integrated TS-DDraw with ThaiFontFix as $activeDll"
Write-Host "Original renderer backup: $originalDll"
$fontConfigTarget = Join-Path $root 'thaifontfix.ini'
if (-not (Test-Path -LiteralPath $fontConfigTarget)) {
    Copy-Item -LiteralPath $fontConfig -Destination $fontConfigTarget
}
Write-Host "ThaiFontFix settings: $fontConfigTarget"
