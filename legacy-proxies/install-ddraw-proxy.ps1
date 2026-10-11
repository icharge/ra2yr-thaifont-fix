param(
    [switch]$Restore
)

$ErrorActionPreference = 'Stop'

$gameRoot = Split-Path -Parent $PSScriptRoot
$proxy = Join-Path $PSScriptRoot 'ddraw-proxy.dll'
$target = Join-Path $gameRoot 'ddraw.dll'
$customBackup = Join-Path $gameRoot 'ddraw-custom.dll'
$proxyBackup = Join-Path $gameRoot 'ddraw-thaifnt-proxy.dll'
$stage = Join-Path $gameRoot 'ddraw.dll.thaifnt-staging'
$proxyStage = Join-Path $gameRoot 'ddraw-thaifnt-proxy.staging'

if ($Restore) {
    if (-not (Test-Path -LiteralPath $target) -or
        -not (Test-Path -LiteralPath $customBackup) -or
        -not (Test-Path -LiteralPath $proxyBackup)) {
        throw 'Restore requires ddraw.dll, ddraw-custom.dll, and ddraw-thaifnt-proxy.dll.'
    }

    $activeHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    $installedProxyHash = (Get-FileHash -LiteralPath $proxyBackup -Algorithm SHA256).Hash
    if ($activeHash -ne $installedProxyHash) {
        throw 'The active ddraw.dll differs from the installed ThaiFontFix proxy. Refusing to overwrite it.'
    }

    Copy-Item -LiteralPath $customBackup -Destination $stage -Force
    Move-Item -LiteralPath $stage -Destination $target -Force
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $customBackup -Algorithm SHA256).Hash) {
        throw 'Restore verification failed. The original backup remains available as ddraw-custom.dll.'
    }
    Write-Host "Restored the original CnCNet/TS-DDraw to $target. Backups remain in the game folder."
    exit 0
}

if (-not (Test-Path -LiteralPath $proxy) -or -not (Test-Path -LiteralPath $target)) {
    throw 'Build ddraw-proxy.dll and make sure the game-folder ddraw.dll exists first.'
}

$proxyHash = (Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash
$currentHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash

if ($currentHash -eq $proxyHash) {
    if (-not (Test-Path -LiteralPath $customBackup)) {
        throw 'ddraw.dll is this proxy, but ddraw-custom.dll is missing; restore the original renderer before continuing.'
    }
    Copy-Item -LiteralPath $proxy -Destination $proxyStage -Force
    Move-Item -LiteralPath $proxyStage -Destination $proxyBackup -Force
    Write-Host 'The ThaiFontFix ddraw proxy is already installed.'
    exit 0
}

$hasCustomBackup = Test-Path -LiteralPath $customBackup
$updating = $false
$reinstalling = $false
if ($hasCustomBackup) {
    if (-not (Test-Path -LiteralPath $proxyBackup)) {
        throw 'ddraw-custom.dll exists but the installed-proxy record is missing. The active DLL was left untouched.'
    }
    $installedProxyHash = (Get-FileHash -LiteralPath $proxyBackup -Algorithm SHA256).Hash
    $originalHash = (Get-FileHash -LiteralPath $customBackup -Algorithm SHA256).Hash
    if ($currentHash -eq $installedProxyHash) {
        $updating = $true
    } elseif ($currentHash -eq $originalHash) {
        $reinstalling = $true
    } else {
        throw 'ddraw.dll is not the recorded ThaiFontFix proxy. It was left untouched.'
    }
} elseif (Test-Path -LiteralPath $proxyBackup) {
    throw 'ddraw-thaifnt-proxy.dll already exists without ddraw-custom.dll. Inspect these files before installing.'
}

Copy-Item -LiteralPath $proxy -Destination $stage -Force
if ((Get-FileHash -LiteralPath $stage -Algorithm SHA256).Hash -ne $proxyHash) {
    Remove-Item -LiteralPath $stage -Force
    throw 'Staged proxy hash did not match the built proxy.'
}

if (-not $hasCustomBackup) {
    Copy-Item -LiteralPath $target -Destination $customBackup
    if ((Get-FileHash -LiteralPath $customBackup -Algorithm SHA256).Hash -ne $currentHash) {
        Remove-Item -LiteralPath $stage -Force
        Remove-Item -LiteralPath $customBackup -Force
        throw 'The preserved ddraw-custom.dll hash did not match the original ddraw.dll.'
    }
}

Move-Item -LiteralPath $stage -Destination $target -Force
if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $proxyHash) {
    $recovery = if ($updating) { $proxyBackup } else { $customBackup }
    Copy-Item -LiteralPath $recovery -Destination $stage -Force
    Move-Item -LiteralPath $stage -Destination $target -Force
    throw 'Proxy installation verification failed; the previous ddraw.dll was restored.'
}

Copy-Item -LiteralPath $proxy -Destination $proxyStage -Force
if ((Get-FileHash -LiteralPath $proxyStage -Algorithm SHA256).Hash -ne $proxyHash) {
    throw 'The new proxy is active, but its update record could not be verified.'
}
Move-Item -LiteralPath $proxyStage -Destination $proxyBackup -Force

if ($updating) {
    Write-Host "Updated the ThaiFontFix ddraw proxy at $target. The original renderer remains $customBackup."
} elseif ($reinstalling) {
    Write-Host "Reinstalled the ThaiFontFix ddraw proxy at $target. The original renderer remains $customBackup."
} else {
    Write-Host "Installed the ThaiFontFix ddraw proxy at $target. The original renderer is preserved as $customBackup."
}
