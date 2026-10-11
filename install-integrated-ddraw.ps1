[CmdletBinding()]
param(
    [switch]$Restore
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$integratedDll = Join-Path $PSScriptRoot 'ddraw-integrated.dll'
$activeDll = Join-Path $root 'ddraw.dll'
$originalDll = Join-Path $root 'ddraw-custom.dll'
$versionProxy = Join-Path $root 'version.dll'
$versionConfig = Join-Path $root 'version-thaifnt.ini'
$legacyDirectory = Join-Path $PSScriptRoot 'legacy-proxies'
$knownVersionProxyHash = '25FDAFC1AD13862A79C2D1BA7E9162E3D0A9712CDF07C99FE46B10913CA00A08'
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

function Move-ToLegacy([string]$Path, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    New-Item -ItemType Directory -Path $legacyDirectory -Force | Out-Null
    $destination = Join-Path $legacyDirectory $Name
    if (Test-Path -LiteralPath $destination) {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $destination = Join-Path $legacyDirectory "$stamp-$Name"
    }
    Move-Item -LiteralPath $Path -Destination $destination
    Write-Host "Archived $Path to $destination"
}

function Archive-KnownVersionProxy {
    if (-not (Test-Path -LiteralPath $versionProxy)) { return }
    $versionHash = Get-Sha256 $versionProxy
    if ($versionHash -ne $knownVersionProxyHash) {
        throw "Found an unrecognized version.dll ($versionHash); refusing to move it."
    }
    Move-ToLegacy $versionProxy 'version-proxy.dll'
    Move-ToLegacy $versionConfig 'version-thaifnt.ini'
}

$gameProcesses = @(Get-Process -Name @('RA2MD', 'GameMD', 'gamemdpatch', 'game') -ErrorAction SilentlyContinue)
if ($gameProcesses.Count -gt 0) {
    $names = ($gameProcesses | Select-Object -ExpandProperty ProcessName -Unique) -join ', '
    throw "Close the game before changing its renderer DLL. Still running: $names"
}

if (-not (Test-Path -LiteralPath $activeDll)) { throw "Game-folder ddraw.dll not found: $activeDll" }
if (-not (Test-Path -LiteralPath $originalDll)) {
    Copy-Item -LiteralPath $activeDll -Destination $originalDll
    Write-Host "Saved the current renderer as $originalDll"
}
$originalHash = Get-Sha256 $originalDll
$currentHash = Get-Sha256 $activeDll

if ($Restore) {
    Archive-KnownVersionProxy
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

Archive-KnownVersionProxy

Copy-Item -LiteralPath $integratedDll -Destination $activeDll -Force
if ((Get-Sha256 $activeDll) -ne $integratedHash) { throw 'Integrated renderer install hash verification failed.' }
Write-Host "Installed integrated TS-DDraw with ThaiFontFix as $activeDll"
Write-Host "Original renderer backup: $originalDll"
Write-Host 'ThaiFontFix settings: ddraw-thaifnt.ini'
