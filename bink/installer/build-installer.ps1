[CmdletBinding()]
param(
    [string]$GameDirectory,
    [string]$VisualStudioPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools',
    [string]$NsisPath,
    [switch]$SkipProxyBuild
)

$ErrorActionPreference = 'Stop'
$installerDirectory = $PSScriptRoot
$binkDirectory = Split-Path -Parent $installerDirectory
$projectRoot = Split-Path -Parent $binkDirectory
$defaultGameDirectory = Split-Path -Parent $projectRoot
$gameRoot = if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
    (Resolve-Path -LiteralPath $defaultGameDirectory).Path
} else {
    (Resolve-Path -LiteralPath $GameDirectory).Path
}

$proxy = Join-Path $binkDirectory 'binkw32.dll'
$config = Join-Path $projectRoot 'thaifontfix.ini'
$readme = Join-Path $installerDirectory 'ThaiFontFix_readme.txt'
$defaultFont = Join-Path $installerDirectory 'assets\fonts\default\game.fnt'
$definition = Join-Path $binkDirectory 'build-artifacts\binkw32.def'
$script = Join-Path $installerDirectory 'ThaiFontFix.nsi'
$buildRoot = Join-Path $installerDirectory 'build-artifacts'
$packageRoot = Join-Path $buildRoot 'package'
$finalOutput = Join-Path $installerDirectory 'ThaiFontFix-Setup.exe'
$generatedInclude = Join-Path $buildRoot 'installer-build.nsh'

if (-not $SkipProxyBuild) {
    $proxyBuildScript = Join-Path $binkDirectory 'build.ps1'
    & $proxyBuildScript -GameDirectory $gameRoot -VisualStudioPath $VisualStudioPath
    if ($LASTEXITCODE -ne 0) {
        throw "The Bink proxy build failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Warning 'Packaging the existing proxy without rebuilding it. Confirm it was built with the XP target toolset before distributing this setup.'
}

$proxyVersion = (Get-Item -LiteralPath $proxy).VersionInfo.FileVersion
if ([string]::IsNullOrWhiteSpace($proxyVersion) -or $proxyVersion -notmatch '^\d+\.\d+\.\d+(?:\.\d+)?$') {
    throw "The Bink proxy has no supported numeric file version: $proxyVersion"
}

foreach ($required in @($proxy, $config, $readme, $defaultFont, $definition, $script)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required installer input is missing: $required"
    }
}

$installerSource = Get-Content -LiteralPath $script -Raw
if ($installerSource -match '(?im)^\s*WriteUninstaller\b') {
    throw 'This installer intentionally has no uninstaller. Use the Restore option to remove the proxy.'
}

$exportEntries = @()
foreach ($line in Get-Content -LiteralPath $definition) {
    if ($line -match '^\s*(?<name>[^=\s]+)=_BinkProxy\d+@0 @(?<ordinal>\d+)\s*$') {
        $name = $Matches.name
        $ordinal = [int]$Matches.ordinal
        if ($name -match '[|\r\n]') { throw "Unsupported Bink export name: $name" }
        $exportEntries += [pscustomobject]@{ Ordinal = $ordinal; Name = $name }
    }
}
if ($exportEntries.Count -eq 0) { throw "No original Bink exports were found in $definition" }
if (@($exportEntries | Where-Object Name -eq 'ThaiFontFixIsActive').Count -ne 0) {
    throw 'The export definition includes the ThaiFontFix marker as an original Bink export.'
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
$staleProxyPackage = Join-Path $packageRoot 'binkw32-proxy.dll'
if (Test-Path -LiteralPath $staleProxyPackage -PathType Leaf) {
    Remove-Item -LiteralPath $staleProxyPackage -Force
}
Copy-Item -LiteralPath $proxy -Destination (Join-Path $packageRoot 'binkw32.dll') -Force
Copy-Item -LiteralPath $config -Destination (Join-Path $packageRoot 'thaifontfix.ini') -Force
Copy-Item -LiteralPath $readme -Destination (Join-Path $packageRoot 'ThaiFontFix_readme.txt') -Force
$fontPackagePath = Join-Path $packageRoot 'fonts\default\game.fnt'
New-Item -ItemType Directory -Path (Split-Path -Parent $fontPackagePath) -Force | Out-Null
Copy-Item -LiteralPath $defaultFont -Destination $fontPackagePath -Force
$exportLines = foreach ($entry in $exportEntries) {
    '{0}|{1}' -f $entry.Ordinal, $entry.Name
}
Set-Content -LiteralPath (Join-Path $packageRoot 'expected-exports.txt') -Value $exportLines -Encoding Ascii

$includeLines = @(
    '!define PROXY_FILE "build-artifacts\package\binkw32.dll"',
    ('!define PROXY_VERSION "{0}"' -f $proxyVersion),
    '!define CONFIG_FILE "build-artifacts\package\thaifontfix.ini"',
    '!define README_FILE "build-artifacts\package\ThaiFontFix_readme.txt"',
    '!define DEFAULT_FONT_FILE "build-artifacts\package\fonts\default\game.fnt"',
    '!define EXPORTS_FILE "build-artifacts\package\expected-exports.txt"',
    '!define OUTPUT_FILE "build-artifacts\ThaiFontFix-Setup.exe"'
)
Set-Content -LiteralPath $generatedInclude -Value $includeLines -Encoding Ascii

if ([string]::IsNullOrWhiteSpace($NsisPath)) {
    $installedNsis = Get-Command 'makensis.exe' -ErrorAction SilentlyContinue
    if ($installedNsis) {
        $NsisPath = $installedNsis.Source
    } else {
        $candidates = @(
            (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'),
            (Join-Path $env:ProgramFiles 'NSIS\makensis.exe'),
            (Join-Path $buildRoot 'nsis-3.13\nsis-3.13\Bin\makensis.exe'),
            (Join-Path $buildRoot 'nsis-3.13\nsis-3.13\makensis.exe')
        ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
        if ($candidates.Count -gt 0) { $NsisPath = $candidates[0] }
    }
}
if ([string]::IsNullOrWhiteSpace($NsisPath) -or -not (Test-Path -LiteralPath $NsisPath -PathType Leaf)) {
    throw 'makensis.exe was not found. Install NSIS 3.x or pass -NsisPath <path to makensis.exe>.'
}
$NsisPath = (Resolve-Path -LiteralPath $NsisPath).Path

Push-Location $installerDirectory
try {
    & $NsisPath '/V2' 'ThaiFontFix.nsi'
    if ($LASTEXITCODE -ne 0) { throw "makensis failed with exit code $LASTEXITCODE." }
} finally {
    Pop-Location
}

$builtOutput = Join-Path $buildRoot 'ThaiFontFix-Setup.exe'
if (-not (Test-Path -LiteralPath $builtOutput -PathType Leaf)) { throw "NSIS did not produce $builtOutput" }

# Confirm the generated setup is a 32-bit GUI executable. NSIS 3 installers
# support Windows XP; the proxy DLL is separately built with v141_xp.
$stream = [System.IO.File]::OpenRead($builtOutput)
try {
    $reader = [System.IO.BinaryReader]::new($stream)
    if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'NSIS output is missing the DOS PE signature.' }
    $stream.Position = 0x3C
    $peOffset = $reader.ReadInt32()
    $stream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x00004550) { throw 'NSIS output is missing the PE signature.' }
    if ($reader.ReadUInt16() -ne 0x014C) { throw 'NSIS output is not an x86 executable.' }
    $stream.Position = $peOffset + 24
    if ($reader.ReadUInt16() -ne 0x010B) { throw 'NSIS output is not a PE32 executable.' }
    $stream.Position = $peOffset + 24 + 68
    if ($reader.ReadUInt16() -ne 2) { throw 'NSIS output is not a Windows GUI executable.' }
} finally {
    $stream.Dispose()
}

Copy-Item -LiteralPath $builtOutput -Destination $finalOutput -Force
$result = Get-Item -LiteralPath $finalOutput
Write-Host "Built NSIS installer: $($result.FullName) ($($result.Length) bytes)"
Write-Host "Bundled $($exportEntries.Count) original Bink export checks."
