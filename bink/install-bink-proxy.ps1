[CmdletBinding()]
param(
    [switch]$Restore,
    [string]$GameDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$defaultGameDirectory = Split-Path -Parent $projectRoot
$root = if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
    (Resolve-Path -LiteralPath $defaultGameDirectory).Path
} else {
    (Resolve-Path -LiteralPath $GameDirectory).Path
}

$proxy = Join-Path $PSScriptRoot 'binkw32-proxy.dll'
$target = Join-Path $root 'binkw32.dll'
$realDll = Join-Path $root 'binkw32.bin'
$oldTlbDll = Join-Path $root 'BINKW32.TLB'
$legacyRealDll = Join-Path $root 'ThaiFontFixBinkReal.dll'
$proxyRecord = Join-Path $root 'BINKW32-thaifnt-proxy.dll'
$originalHashFile = Join-Path $root 'BINKW32-thaifnt-original.sha256'
$stage = Join-Path $root 'binkw32.dll.thaifnt-staging'
$removeStage = Join-Path $root 'binkw32.dll.thaifnt-removing'
$configSource = Join-Path $projectRoot 'thaifontfix.ini'
$configTarget = Join-Path $root 'thaifontfix.ini'

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-OriginalCandidates {
    return @($realDll, $oldTlbDll, $legacyRealDll) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
}

function Remove-LegacyRecords {
    Remove-Item -LiteralPath $proxyRecord, $originalHashFile -Force -ErrorAction SilentlyContinue
}

function Restore-Original {
    $candidates = @(Get-OriginalCandidates)
    if ($candidates.Count -ne 1) {
        throw 'Expected exactly one preserved original Bink DLL (binkw32.bin or a legacy backup); files were left unchanged.'
    }
    if (-not (Test-Path -LiteralPath $target -PathType Leaf) -or
        (Get-Item -LiteralPath $target).VersionInfo.FileDescription -ne 'ThaiFontFix BINKW32 Proxy') {
        throw 'binkw32.dll is not the ThaiFontFix proxy; files were left unchanged.'
    }

    $backup = $candidates[0]
    $hasProxyRecord = Test-Path -LiteralPath $proxyRecord -PathType Leaf
    $hasHashRecord = Test-Path -LiteralPath $originalHashFile -PathType Leaf
    if ($hasProxyRecord -ne $hasHashRecord) {
        throw 'Legacy install records are incomplete; files were left unchanged.'
    }
    if ($hasProxyRecord -and (Get-Sha256 $target) -ne (Get-Sha256 $proxyRecord)) {
        throw 'binkw32.dll differs from the recorded ThaiFontFix proxy; files were left unchanged.'
    }
    if ($hasHashRecord) {
        $expectedHash = (Get-Content -LiteralPath $originalHashFile -Raw).Trim()
        if ($expectedHash -notmatch '^[0-9a-fA-F]{64}$' -or (Get-Sha256 $backup) -ne $expectedHash) {
            throw 'The preserved original Bink DLL failed its recorded hash check; files were left unchanged.'
        }
    }
    if (Test-Path -LiteralPath $removeStage) { throw "A stale restore staging file exists: $removeStage" }

    $backupHash = Get-Sha256 $backup
    Move-Item -LiteralPath $target -Destination $removeStage
    try {
        Move-Item -LiteralPath $backup -Destination $target
        if ((Get-Sha256 $target) -ne $backupHash -or
            (Get-Item -LiteralPath $target).VersionInfo.FileDescription -eq 'ThaiFontFix BINKW32 Proxy') {
            throw 'Restored BINKW32.DLL failed verification.'
        }
        Remove-Item -LiteralPath $removeStage -Force
        Remove-LegacyRecords
        Write-Host "Restored the original Bink DLL at $target"
    } catch {
        if ((Test-Path -LiteralPath $target -PathType Leaf) -and (Get-Sha256 $target) -eq $backupHash) {
            Move-Item -LiteralPath $target -Destination $backup
        }
        if ((Test-Path -LiteralPath $removeStage -PathType Leaf) -and -not (Test-Path -LiteralPath $target)) {
            Move-Item -LiteralPath $removeStage -Destination $target
        }
        throw
    }
}

foreach ($filePath in @($target, $realDll, $oldTlbDll, $legacyRealDll, $proxyRecord, $originalHashFile, $stage, $removeStage)) {
    if ((Test-Path -LiteralPath $filePath) -and -not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
        throw "Expected a file path but found a directory or non-file entry: $filePath"
    }
}

$gameProcesses = @(Get-Process -Name @('RA2MD', 'GameMD', 'gamemd', 'gamemdpatch', 'game', 'Syringe') -ErrorAction SilentlyContinue)
if ($gameProcesses.Count -gt 0) {
    $names = ($gameProcesses | Select-Object -ExpandProperty ProcessName -Unique) -join ', '
    throw "Close RA2/Yuri's Revenge and Syringe before changing binkw32.dll. Still running: $names"
}

if ($Restore) {
    Restore-Original
    return
}

foreach ($path in @($proxy, $configSource)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required Bink install file is missing: $path" }
}
if ((Get-Item -LiteralPath $proxy).VersionInfo.FileDescription -ne 'ThaiFontFix BINKW32 Proxy') {
    throw "The built DLL is not the expected ThaiFontFix BINKW32 proxy: $proxy"
}

# Upgrade the previous .TLB / legacy layout safely before installing the .bin layout.
if ((Test-Path -LiteralPath $target -PathType Leaf) -and
    (Get-Item -LiteralPath $target).VersionInfo.FileDescription -eq 'ThaiFontFix BINKW32 Proxy') {
    Restore-Original
}
if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "The original Bink DLL is missing: $target" }
if ((Get-Item -LiteralPath $target).VersionInfo.FileDescription -eq 'ThaiFontFix BINKW32 Proxy') {
    throw 'binkw32.dll is already a ThaiFontFix proxy without a recoverable original; it was left untouched.'
}
if (@(Get-OriginalCandidates).Count -ne 0) {
    throw 'A preserved Bink backup already exists; refusing to overwrite it.'
}
if ((Test-Path -LiteralPath $proxyRecord) -or (Test-Path -LiteralPath $originalHashFile)) {
    throw 'Stale legacy proxy/hash records exist without an active recoverable install; files were left untouched.'
}
foreach ($stalePath in @($stage, $removeStage)) {
    if (Test-Path -LiteralPath $stalePath) { throw "A stale install staging file exists: $stalePath" }
}

$originalHash = Get-Sha256 $target
$proxyHash = Get-Sha256 $proxy
Copy-Item -LiteralPath $proxy -Destination $stage
if ((Get-Sha256 $stage) -ne $proxyHash) {
    Remove-Item -LiteralPath $stage -Force -ErrorAction SilentlyContinue
    throw 'Staged Bink proxy failed its hash check; the original DLL was not changed.'
}

$originalMoved = $false
$proxyInstalled = $false
try {
    if ((Get-Sha256 $target) -ne $originalHash) { throw 'binkw32.dll changed during install; it was left untouched.' }
    Move-Item -LiteralPath $target -Destination $realDll
    $originalMoved = $true
    if ((Get-Sha256 $realDll) -ne $originalHash) { throw 'Renamed original Bink DLL failed its hash check.' }

    Move-Item -LiteralPath $stage -Destination $target
    $proxyInstalled = $true
    if ((Get-Sha256 $target) -ne $proxyHash -or
        (Get-Item -LiteralPath $target).VersionInfo.FileDescription -ne 'ThaiFontFix BINKW32 Proxy') {
        throw 'Installed Bink proxy failed verification.'
    }
} catch {
    if ($proxyInstalled -and (Test-Path -LiteralPath $target -PathType Leaf) -and (Get-Sha256 $target) -eq $proxyHash) {
        Remove-Item -LiteralPath $target -Force
    }
    if ($originalMoved -and (Test-Path -LiteralPath $realDll -PathType Leaf) -and
        (Get-Sha256 $realDll) -eq $originalHash -and -not (Test-Path -LiteralPath $target)) {
        Move-Item -LiteralPath $realDll -Destination $target
    }
    Remove-Item -LiteralPath $stage -Force -ErrorAction SilentlyContinue
    throw
}

if (-not (Test-Path -LiteralPath $configTarget -PathType Leaf)) {
    Copy-Item -LiteralPath $configSource -Destination $configTarget
}
Write-Host "Installed the BINKW32 proxy at $target"
Write-Host "The original Bink DLL is preserved as $realDll. No proxy or SHA-256 sidecar files are created."
Write-Host "Restore with .\install-bink-proxy.ps1 -Restore"
