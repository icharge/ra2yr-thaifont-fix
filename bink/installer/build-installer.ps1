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
$proxyVersion = (Get-Item -LiteralPath $proxy).VersionInfo.FileVersion
if ([string]::IsNullOrWhiteSpace($proxyVersion) -or $proxyVersion -notmatch '^\d+\.\d+\.\d+(?:\.\d+)?$') {
    throw "The Bink proxy has no supported numeric file version: $proxyVersion"
}
$config = Join-Path $projectRoot 'thaifontfix.ini'
$readme = Join-Path $installerDirectory 'ThaiFontFix_readme.txt'
$defaultFont = Join-Path $installerDirectory 'assets\fonts\default\game.fnt'
$upstreamFontsRoot = Join-Path $projectRoot 'fonts\FontsForRedAlert2\Source'
$fontPackReadme = Join-Path $projectRoot 'fonts\FontsForRedAlert2\README.md'
$fontPackLicense = Join-Path $projectRoot 'fonts\FontsForRedAlert2\LICENSE.md'
$fontPreviewRenderer = Join-Path $installerDirectory 'render-font-preview.py'
$fontVariants = @(
    [pscustomobject]@{ Id = 'boiling-point'; Define = 'BOILING_POINT_FONT_FILE'; PreviewDefine = 'BOILING_POINT_PREVIEW_FILE'; AuthorDefine = 'BOILING_POINT_FONT_AUTHOR'; Author = 'wiktorderelf; edits by mah_boi'; Source = Join-Path $upstreamFontsRoot 'BoilingPointFont\game.fnt' },
    [pscustomobject]@{ Id = 'final-war'; Define = 'FINAL_WAR_FONT_FILE'; PreviewDefine = 'FINAL_WAR_PREVIEW_FILE'; AuthorDefine = 'FINAL_WAR_FONT_AUTHOR'; Author = 'SMxReaver; edits by mah_boi'; Source = Join-Path $upstreamFontsRoot 'FinalWarFont\game.fnt' },
    [pscustomobject]@{ Id = 'phobos-improved'; Define = 'PHOBOS_IMPROVED_FONT_FILE'; PreviewDefine = 'PHOBOS_IMPROVED_PREVIEW_FILE'; AuthorDefine = 'PHOBOS_IMPROVED_FONT_AUTHOR'; Author = 'Kerbiter, wiktorderelf, Uranusian; edits by mah_boi'; Source = Join-Path $upstreamFontsRoot 'PhobosImprovedFont\game.fnt' },
    [pscustomobject]@{ Id = 'reborn'; Define = 'REBORN_FONT_FILE'; PreviewDefine = 'REBORN_PREVIEW_FILE'; AuthorDefine = 'REBORN_FONT_AUTHOR'; Author = 'Belonit'; Source = Join-Path $upstreamFontsRoot 'RebornFont\game.fnt' },
    [pscustomobject]@{ Id = 'rise-of-the-east'; Define = 'RISE_OF_THE_EAST_FONT_FILE'; PreviewDefine = 'RISE_OF_THE_EAST_PREVIEW_FILE'; AuthorDefine = 'RISE_OF_THE_EAST_FONT_AUTHOR'; Author = 'ayylmao; edits by mah_boi'; Source = Join-Path $upstreamFontsRoot 'RiseOfTheEastFont\game.fnt' },
    [pscustomobject]@{ Id = 'westwood-improved'; Define = 'WESTWOOD_IMPROVED_FONT_FILE'; PreviewDefine = 'WESTWOOD_IMPROVED_PREVIEW_FILE'; AuthorDefine = 'WESTWOOD_IMPROVED_FONT_AUTHOR'; Author = 'Westwood; edits by mah_boi'; Source = Join-Path $upstreamFontsRoot 'WestwoodImprovedFont\game.fnt' }
)

function Assert-ThaiGameFont {
    param([Parameter(Mandatory)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $mapOffset = 28
    $recordsOffset = $mapOffset + 0x20000
    if ($bytes.Length -lt $recordsOffset -or [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'fonT') {
        throw "Not a Red Alert 2 game.fnt file: $Path"
    }
    $height = [BitConverter]::ToUInt32($bytes, 12)
    $count = [BitConverter]::ToUInt32($bytes, 20)
    $stride = [BitConverter]::ToUInt32($bytes, 24)
    if ($height -ne 16 -or $count -eq 0 -or $stride -lt 49 -or $bytes.Length -ne ($recordsOffset + [long]$count * $stride)) {
        throw "Unsupported game.fnt dimensions in $Path (height=$height, stride=$stride, glyphs=$count)."
    }

    $thaiCodes = @((0xA1..0xDA) + 0xDF + (0xE0..0xFB))
    foreach ($code in $thaiCodes) {
        $entry = [BitConverter]::ToUInt16($bytes, $mapOffset + $code * 2)
        if ($entry -eq 0 -or $entry -gt $count) {
            throw "Font $Path has no valid Thai CP874 glyph for byte 0x$('{0:X2}' -f $code)."
        }
    }

    $markCodes = @(0xD1, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE)
    $markEntries = @{}
    foreach ($code in $markCodes) {
        $entry = [BitConverter]::ToUInt16($bytes, $mapOffset + $code * 2)
        if ($entry -eq 0 -or $entry -gt $count) {
            throw "Font $Path has an invalid Thai mark mapping for byte 0x$('{0:X2}' -f $code)."
        }
        $markEntries[[string]$entry] = $true
    }
    for ($code = 0; $code -le 0xFF; ++$code) {
        if ($markCodes -contains $code) { continue }
        $entry = [BitConverter]::ToUInt16($bytes, $mapOffset + $code * 2)
        if ($entry -ne 0 -and $markEntries.ContainsKey([string]$entry)) {
            throw "Font $Path aliases Thai mark glyph $entry to byte 0x$('{0:X2}' -f $code)."
        }
    }
}

function Get-InstallerGameFont {
    param([Parameter(Mandatory)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $recordsOffset = 28 + 0x20000
    if ($bytes.Length -lt $recordsOffset -or [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'fonT') {
        throw "Not a Red Alert 2 game.fnt file: $Path"
    }
    $height = [BitConverter]::ToUInt32($bytes, 12)
    $count = [BitConverter]::ToUInt32($bytes, 20)
    $stride = [BitConverter]::ToUInt32($bytes, 24)
    if ($height -ne 16 -or $count -eq 0 -or $count -gt [uint16]::MaxValue -or
        ($stride -ne 33 -and $stride -ne 49) -or
        $bytes.Length -ne ($recordsOffset + [long]$count * $stride)) {
        throw "Unsupported game.fnt dimensions in $Path (height=$height, stride=$stride, glyphs=$count)."
    }

    if ($stride -eq 33) {
        # Reborn stores two bitmap bytes per row; the game hook expects the
        # three-byte rows used by the other RA2 fonts. Expand it in the package.
        $expanded = New-Object byte[] ($recordsOffset + [long]$count * 49)
        [Array]::Copy($bytes, 0, $expanded, 0, $recordsOffset)
        [BitConverter]::GetBytes([uint32]49).CopyTo($expanded, 24)
        for ($glyph = 0; $glyph -lt $count; ++$glyph) {
            $sourceRecord = $recordsOffset + $glyph * 33
            $targetRecord = $recordsOffset + $glyph * 49
            $expanded[$targetRecord] = $bytes[$sourceRecord]
            if ($expanded[$targetRecord] -gt 16) {
                throw "Invalid glyph width $($expanded[$targetRecord]) in $Path at record $glyph."
            }
            for ($row = 0; $row -lt 16; ++$row) {
                [Array]::Copy($bytes, $sourceRecord + 1 + $row * 2, $expanded, $targetRecord + 1 + $row * 3, 2)
            }
        }
        $bytes = $expanded
        $stride = 49
    }

    [pscustomobject]@{ Bytes = $bytes; Count = [uint32]$count; Stride = [uint32]$stride }
}

function Merge-InstallerThaiGlyphs {
    param(
        [Parameter(Mandatory)][string]$FontSource,
        [Parameter(Mandatory)][string]$ThaiSource,
        [Parameter(Mandatory)][string]$Destination
    )

    $recordsOffset = 28 + 0x20000
    $font = Get-InstallerGameFont -Path $FontSource
    $thai = Get-InstallerGameFont -Path $ThaiSource
    $thaiCodes = @((0xA1..0xDA) + 0xDF + (0xE0..0xFB))
    $fontBytes = $font.Bytes
    $thaiBytes = $thai.Bytes

    foreach ($code in $thaiCodes) {
        $sourceIndex = [BitConverter]::ToUInt16($thaiBytes, 28 + $code * 2)
        if ($sourceIndex -eq 0 -or $sourceIndex -gt $thai.Count) {
            throw "Thai source font $ThaiSource has no valid glyph for CP874 byte 0x$('{0:X2}' -f $code)."
        }
    }
    $mergedCount = [long]$font.Count + $thaiCodes.Count
    if ($mergedCount -gt [uint16]::MaxValue) {
        throw "Merging Thai glyphs into $FontSource would exceed the FNT map index limit."
    }

    $merged = New-Object byte[] ($recordsOffset + $mergedCount * 49)
    [Array]::Copy($fontBytes, 0, $merged, 0, $fontBytes.Length)
    [BitConverter]::GetBytes([uint32]$mergedCount).CopyTo($merged, 20)
    [BitConverter]::GetBytes([uint32]49).CopyTo($merged, 24)

    for ($i = 0; $i -lt $thaiCodes.Count; ++$i) {
        $code = $thaiCodes[$i]
        $sourceIndex = [BitConverter]::ToUInt16($thaiBytes, 28 + $code * 2)
        $sourceRecord = $recordsOffset + ([long]$sourceIndex - 1) * 49
        $targetIndex = [long]$font.Count + $i + 1
        $targetRecord = $recordsOffset + ($targetIndex - 1) * 49
        if ($thaiBytes[$sourceRecord] -gt 16) {
            throw "Invalid Thai glyph width $($thaiBytes[$sourceRecord]) in $ThaiSource for byte 0x$('{0:X2}' -f $code)."
        }
        [Array]::Copy($thaiBytes, $sourceRecord, $merged, $targetRecord, 49)
        [BitConverter]::GetBytes([uint16]$targetIndex).CopyTo($merged, 28 + $code * 2)
    }

    for ($i = 0; $i -lt $thaiCodes.Count; ++$i) {
        $code = $thaiCodes[$i]
        $sourceIndex = [BitConverter]::ToUInt16($thaiBytes, 28 + $code * 2)
        $expectedIndex = [uint16]([long]$font.Count + $i + 1)
        $actualIndex = [BitConverter]::ToUInt16($merged, 28 + $code * 2)
        $sourceRecord = $recordsOffset + ([long]$sourceIndex - 1) * 49
        $targetRecord = $recordsOffset + ([long]$actualIndex - 1) * 49
        if ($actualIndex -ne $expectedIndex) {
            throw "Merged font $Destination has an incorrect CP874 map entry for byte 0x$('{0:X2}' -f $code)."
        }
        for ($byte = 0; $byte -lt 49; ++$byte) {
            if ($merged[$targetRecord + $byte] -ne $thaiBytes[$sourceRecord + $byte]) {
                throw "Merged font $Destination has an incorrect Thai glyph bitmap for byte 0x$('{0:X2}' -f $code)."
            }
        }
    }

    [System.IO.File]::WriteAllBytes($Destination, $merged)
    Assert-ThaiGameFont -Path $Destination
}

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

if (-not (Test-Path -LiteralPath $upstreamFontsRoot -PathType Container)) {
    throw 'The FontsForRedAlert2 submodule is not checked out. Run: git submodule update --init -- fonts/FontsForRedAlert2'
}
foreach ($required in @($proxy, $config, $readme, $defaultFont, $definition, $script, $fontPreviewRenderer, $fontPackReadme, $fontPackLicense) + @($fontVariants | ForEach-Object Source)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required installer input is missing: $required"
    }
}
Assert-ThaiGameFont -Path $defaultFont

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
foreach ($variant in $fontVariants) {
    $variantPackagePath = Join-Path $packageRoot ("fonts\{0}\game.fnt" -f $variant.Id)
    New-Item -ItemType Directory -Path (Split-Path -Parent $variantPackagePath) -Force | Out-Null
    Merge-InstallerThaiGlyphs -FontSource $variant.Source -ThaiSource $defaultFont -Destination $variantPackagePath
}
$pythonLauncher = Get-Command 'py.exe' -ErrorAction SilentlyContinue
$pythonCommand = if ($pythonLauncher) { $pythonLauncher.Source } else {
    $pythonExe = Get-Command 'python.exe' -ErrorAction SilentlyContinue
    if ($pythonExe) { $pythonExe.Source } else { $null }
}
if ([string]::IsNullOrWhiteSpace($pythonCommand)) {
    throw 'Python 3 with Pillow is required to render the font previews included in the setup.'
}
$previewRoot = Join-Path $packageRoot 'previews'
New-Item -ItemType Directory -Path $previewRoot -Force | Out-Null
$fontPreviews = @([pscustomobject]@{ Id = 'default'; Font = $fontPackagePath }) + @(
    $fontVariants | ForEach-Object {
        [pscustomobject]@{ Id = $_.Id; Font = (Join-Path $packageRoot ("fonts\{0}\game.fnt" -f $_.Id)) }
    }
)
foreach ($preview in $fontPreviews) {
    $previewPath = Join-Path $previewRoot ("font-preview-{0}.bmp" -f $preview.Id)
    if ($pythonLauncher) {
        & $pythonCommand -3 $fontPreviewRenderer --font $preview.Font --output $previewPath
    } else {
        & $pythonCommand $fontPreviewRenderer --font $preview.Font --output $previewPath
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Could not render the $($preview.Id) font preview (Python exit code $LASTEXITCODE)."
    }
}
$fontLicensePath = Join-Path $packageRoot 'FontsForRedAlert2-LICENSE.md'
Copy-Item -LiteralPath $fontPackLicense -Destination $fontLicensePath -Force
$fontRepositoryReadmePath = Join-Path $packageRoot 'FontsForRedAlert2-README.md'
Copy-Item -LiteralPath $fontPackReadme -Destination $fontRepositoryReadmePath -Force
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
    '!define DEFAULT_FONT_PREVIEW_FILE "build-artifacts\package\previews\font-preview-default.bmp"',
    '!define DEFAULT_FONT_AUTHOR "Thai glyphs by THAI RA2"',
    '!define FONT_LICENSE_FILE "build-artifacts\package\FontsForRedAlert2-LICENSE.md"',
    '!define FONT_PACK_README_FILE "build-artifacts\package\FontsForRedAlert2-README.md"'
)
foreach ($variant in $fontVariants) {
    $includeLines += ('!define {0} "build-artifacts\package\fonts\{1}\game.fnt"' -f $variant.Define, $variant.Id)
    $includeLines += ('!define {0} "build-artifacts\package\previews\font-preview-{1}.bmp"' -f $variant.PreviewDefine, $variant.Id)
    $includeLines += ('!define {0} "{1}"' -f $variant.AuthorDefine, $variant.Author)
}
$includeLines += @(
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
Write-Host "Bundled $($fontVariants.Count + 1) Thai-capable game fonts."
