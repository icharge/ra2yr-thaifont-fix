param(
    [switch]$Restore
)

$ErrorActionPreference = 'Stop'

$gameRoot = Split-Path -Parent $PSScriptRoot
$proxy = Join-Path $PSScriptRoot 'winmm-proxy.dll'
$target = Join-Path $gameRoot 'winmm.dll'
$proxyRecord = Join-Path $gameRoot 'winmm-thaifnt-proxy.dll'
$stage = Join-Path $gameRoot 'winmm.dll.thaifnt-staging'
$recordStage = Join-Path $gameRoot 'winmm-thaifnt-proxy.staging'

$running = Get-Process -Name @('game', 'GameMD', 'gamemd', 'gamemdpatch') -ErrorAction SilentlyContinue
if ($running) {
    throw 'Close the game before installing or restoring the WinMM proxy.'
}

if ($Restore) {
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Host 'No local winmm.dll is installed; Windows will load its system copy.'
        exit 0
    }
    if (-not (Test-Path -LiteralPath $proxyRecord)) {
        throw 'A local winmm.dll exists without a ThaiFontFix proxy record. Refusing to remove it.'
    }

    $activeHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    $recordHash = (Get-FileHash -LiteralPath $proxyRecord -Algorithm SHA256).Hash
    if ($activeHash -ne $recordHash) {
        throw 'The local winmm.dll differs from the recorded ThaiFontFix proxy. Refusing to remove it.'
    }

    Remove-Item -LiteralPath $target -Force
    Write-Host "Removed the local ThaiFontFix proxy. Windows will load its system winmm.dll. Proxy copy retained at $proxyRecord."
    exit 0
}

if (-not (Test-Path -LiteralPath $proxy)) {
    throw 'Build winmm-proxy.dll before installing.'
}

$proxyHash = (Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash
if (Test-Path -LiteralPath $target) {
    if (-not (Test-Path -LiteralPath $proxyRecord)) {
        throw 'A local winmm.dll already exists and is not recorded as a ThaiFontFix proxy. It was left untouched.'
    }
    $activeHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    $recordHash = (Get-FileHash -LiteralPath $proxyRecord -Algorithm SHA256).Hash
    if ($activeHash -ne $recordHash) {
        throw 'The local winmm.dll differs from the recorded ThaiFontFix proxy. It was left untouched.'
    }
    if ($activeHash -eq $proxyHash) {
        Write-Host 'The ThaiFontFix WinMM proxy is already installed.'
        exit 0
    }
} elseif (Test-Path -LiteralPath $proxyRecord) {
    throw 'The proxy record exists but local winmm.dll is missing. Inspect the installation before continuing.'
}

Copy-Item -LiteralPath $proxy -Destination $stage -Force
if ((Get-FileHash -LiteralPath $stage -Algorithm SHA256).Hash -ne $proxyHash) {
    Remove-Item -LiteralPath $stage -Force
    throw 'Staged proxy hash did not match the built proxy.'
}

Move-Item -LiteralPath $stage -Destination $target -Force
if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $proxyHash) {
    Remove-Item -LiteralPath $target -Force
    throw 'Installed proxy hash did not match the built proxy.'
}

Copy-Item -LiteralPath $proxy -Destination $recordStage -Force
if ((Get-FileHash -LiteralPath $recordStage -Algorithm SHA256).Hash -ne $proxyHash) {
    throw 'The proxy is active, but its record copy could not be verified.'
}
Move-Item -LiteralPath $recordStage -Destination $proxyRecord -Force

Write-Host "Installed the ThaiFontFix WinMM proxy at $target. Restore with ./install-winmm-proxy.ps1 -Restore."
