[CmdletBinding()]
param(
    [ValidateSet('Both', 'RA2', 'YR')]
    [string]$Variant = 'Both',
    [switch]$Restore,
    [string]$GameDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$defaultGameDirectory = Split-Path -Parent $projectRoot
if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
    $root = (Resolve-Path -LiteralPath $defaultGameDirectory).Path
} else {
    $root = (Resolve-Path -LiteralPath $GameDirectory).Path
}

$manifest = Join-Path $root 'ThaiFontFixSyringe-install.json'
$buildRoot = Join-Path $PSScriptRoot 'build-artifacts'
$variants = @('RA2', 'YR')
if ($Variant -ne 'Both') { $variants = @($Variant) }

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-VariantFiles([string]$Name) {
    $dllName = "ThaiFontFixSyringe-$Name.dll"
    $buildDirectory = Join-Path $buildRoot "ThaiFontFixSyringe-$Name"
    return @(
        @{ Source = Join-Path $buildDirectory $dllName; Name = $dllName },
        @{ Source = Join-Path $PSScriptRoot ($dllName + '.inj'); Name = $dllName + '.inj' }
    )
}

$running = @(Get-Process -Name @('RA2MD', 'GameMD', 'gamemd', 'game', 'Syringe') -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    $names = ($running | Select-Object -ExpandProperty ProcessName -Unique) -join ', '
    throw "Close RA2/Yuri's Revenge and Syringe before installing or restoring. Still running: $names"
}

if ($Restore) {
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        Write-Host 'No ThaiFontFix Syringe installation record is present.'
        exit 0
    }
    $record = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if ($record.schema -ne 1 -or $record.files.Count -eq 0) {
        throw "The Syringe install record is invalid; refusing to remove files: $manifest"
    }
    foreach ($item in $record.files) {
        if ($item.name -notmatch '^ThaiFontFixSyringe-(RA2|YR)\.dll(\.inj)?$' -or
            $item.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw "The Syringe install record contains an unexpected file entry; refusing restore: $manifest"
        }
        $target = Join-Path $root $item.name
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { continue }
        if ((Get-Sha256 $target) -ne $item.sha256.ToLowerInvariant()) {
            throw "Installed file changed since installation; refusing to remove it: $target"
        }
    }
    foreach ($item in $record.files) {
        $target = Join-Path $root $item.name
        if (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target -Force }
    }
    Remove-Item -LiteralPath $manifest -Force
    Write-Host 'Removed ThaiFontFix Syringe DLLs and hook sidecars. INI settings and diagnostic logs were kept.'
    exit 0
}

$configSource = Join-Path $projectRoot 'thaifontfix.ini'
if (-not (Test-Path -LiteralPath $configSource -PathType Leaf)) { throw "Configuration template is missing: $configSource" }
if (Test-Path -LiteralPath $manifest) { throw "A ThaiFontFix Syringe install record already exists: $manifest. Restore it before installing again." }
$files = @()
foreach ($name in $variants) {
    foreach ($file in (Get-VariantFiles $name)) {
        if (-not (Test-Path -LiteralPath $file.Source -PathType Leaf)) {
            throw "Required build or install file is missing: $($file.Source). Run .\syringe\build.ps1 first."
        }
        $files += $file
    }
}

foreach ($file in $files) {
    $target = Join-Path $root $file.Name
    if (Test-Path -LiteralPath $target) { throw "Refusing to overwrite an existing file: $target" }
    $file.Target = $target
    $file.Staging = $target + '.thaifnt-staging'
    if (Test-Path -LiteralPath $file.Staging) { throw "A stale staging file exists: $($file.Staging)" }
    $file.Hash = Get-Sha256 $file.Source
}

$installed = @()
try {
    foreach ($file in $files) {
        Copy-Item -LiteralPath $file.Source -Destination $file.Staging
        if ((Get-Sha256 $file.Staging) -ne $file.Hash) { throw "Staged file hash mismatch: $($file.Staging)" }
    }
    foreach ($file in $files) {
        Move-Item -LiteralPath $file.Staging -Destination $file.Target
        $installed += $file
    }
    $recordFiles = @($installed | Where-Object { $_.Name -match '\.dll(\.inj)?$' } | ForEach-Object {
        @{ name = $_.Name; sha256 = $_.Hash }
    })
    $record = @{ schema = 1; variants = $variants; files = $recordFiles }
    $recordStage = $manifest + '.staging'
    if (Test-Path -LiteralPath $recordStage) { throw "A stale install record staging file exists: $recordStage" }
    $record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $recordStage -Encoding UTF8
    Move-Item -LiteralPath $recordStage -Destination $manifest
} catch {
    foreach ($file in $installed) {
        if ((Test-Path -LiteralPath $file.Target -PathType Leaf) -and (Get-Sha256 $file.Target) -eq $file.Hash) {
            Remove-Item -LiteralPath $file.Target -Force
        }
    }
    foreach ($file in $files) {
        if (Test-Path -LiteralPath $file.Staging -PathType Leaf) { Remove-Item -LiteralPath $file.Staging -Force }
    }
    if (Test-Path -LiteralPath ($manifest + '.staging')) { Remove-Item -LiteralPath ($manifest + '.staging') -Force }
    throw
}

$configTarget = Join-Path $root 'thaifontfix.ini'
if (-not (Test-Path -LiteralPath $configTarget)) { Copy-Item -LiteralPath $configSource -Destination $configTarget }

Write-Host "Installed ThaiFontFix Syringe variant(s): $($variants -join ', ') in $root"
Write-Host 'Restore with .\syringe\install-syringe.ps1 -Restore. Existing INI files are preserved.'
