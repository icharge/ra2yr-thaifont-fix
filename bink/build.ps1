[CmdletBinding()]
param(
    [string]$GameDirectory,
    [string]$VisualStudioPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools',
    [switch]$PrepareOnly,
    [string]$BuiltDllPath
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$defaultGameDirectory = Split-Path -Parent $projectRoot
$gameRoot = if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
    (Resolve-Path -LiteralPath $defaultGameDirectory).Path
} else {
    (Resolve-Path -LiteralPath $GameDirectory).Path
}

$source = Join-Path $PSScriptRoot 'BinkProxy.cpp'
$sharedSource = Join-Path $projectRoot 'shared\ThaiFontFix.cpp'
$resourceSource = Join-Path $PSScriptRoot 'bink-version.rc'
$buildRoot = Join-Path $PSScriptRoot 'build-artifacts'
$definition = Join-Path $buildRoot 'binkw32.def'
$exportsHeader = Join-Path $buildRoot 'BinkExports.generated.h'
$proxyFunctions = Join-Path $buildRoot 'BinkProxyFunctions.generated.inc'
$objectRoot = Join-Path $buildRoot 'obj'
$proxyObject = Join-Path $objectRoot 'BinkProxy.obj'
$sharedObject = Join-Path $objectRoot 'ThaiFontFix.obj'
$resource = Join-Path $buildRoot 'bink-version.res'
$linkedOutput = Join-Path $buildRoot 'binkw32.dll'
$output = Join-Path $PSScriptRoot 'binkw32.dll'
$commandFile = Join-Path $buildRoot 'build-bink-proxy.cmd'
$vcvars = Join-Path $VisualStudioPath 'VC\Auxiliary\Build\vcvarsall.bat'
$resourceCompiler = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x86\rc.exe'
$toolRoot = Join-Path $VisualStudioPath 'VC\Tools\MSVC'

foreach ($required in @($source, $sharedSource, $resourceSource, $vcvars, $resourceCompiler)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required Bink proxy build input is missing: $required"
    }
}

$toolset = Get-ChildItem -LiteralPath $toolRoot -Directory |
    Where-Object { $_.Name -match '^14\.16\.' } |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1
if ($null -eq $toolset) {
    throw "Windows XP builds require the Visual Studio v141_xp component (C++ Windows XP Support for VS 2017 tools). Add it in Visual Studio Installer, then rebuild."
}
$dumpbin = Join-Path $toolset.FullName 'bin\Hostx86\x86\dumpbin.exe'
if (-not (Test-Path -LiteralPath $dumpbin -PathType Leaf)) { throw "x86 DUMPBIN not found: $dumpbin" }

$realDll = Join-Path $gameRoot 'binkw32.bin'
$previousRealDll = Join-Path $gameRoot 'BINKW32.TLB'
$legacyRealDll = Join-Path $gameRoot 'ThaiFontFixBinkReal.dll'
$originalDll = Join-Path $gameRoot 'binkw32.dll'
if (Test-Path -LiteralPath $realDll -PathType Leaf) {
    $exportSource = $realDll
} elseif (Test-Path -LiteralPath $previousRealDll -PathType Leaf) {
    # Read installs from the temporary .TLB naming so they can be rebuilt and migrated.
    $exportSource = $previousRealDll
} elseif (Test-Path -LiteralPath $legacyRealDll -PathType Leaf) {
    # Read older installs so they can be restored and reinstalled with the .TLB target.
    $exportSource = $legacyRealDll
} elseif (Test-Path -LiteralPath $originalDll -PathType Leaf) {
    $description = (Get-Item -LiteralPath $originalDll).VersionInfo.FileDescription
    if ($description -eq 'ThaiFontFix BINKW32 Proxy') {
        throw "BINKW32.DLL is already the ThaiFontFix proxy but its real-DLL target is missing: $realDll"
    }
    $exportSource = $originalDll
} else {
    throw "Could not find the game's original BINKW32.DLL or its renamed proxy target in $gameRoot"
}

$exportOutput = & $dumpbin /nologo /exports $exportSource 2>&1
if ($LASTEXITCODE -ne 0) { throw "DUMPBIN could not read exports from $exportSource" }
$exports = @()
foreach ($line in $exportOutput) {
    if ($line -match '^\s*(\d+)\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$') {
        $ordinal = [int]$Matches[1]
        $name = $Matches[2]
        if ($name -notmatch '^[\x21-\x7e]+$') { throw "Unsupported Bink export name: $name" }
        $exports += [pscustomobject]@{ Ordinal = $ordinal; Name = $name }
    }
}
if ($exports.Count -eq 0) { throw "No named exports were found in $exportSource" }
if (@($exports | Where-Object { $_.Name -eq 'ThaiFontFixIsActive' }).Count -ne 0) {
    throw 'The export source is already a ThaiFontFix proxy; use the preserved original Bink file as the export source.'
}
$markerOrdinal = (($exports | Measure-Object -Property Ordinal -Maximum).Maximum) + 1

New-Item -ItemType Directory -Path $buildRoot, $objectRoot -Force | Out-Null
$definitionLines = @('LIBRARY binkw32', 'EXPORTS')
$headerLines = @("#define BINK_EXPORT_COUNT $($exports.Count)", 'static const char* const g_binkExportNames[BINK_EXPORT_COUNT] = {')
$functionLines = @()
for ($index = 0; $index -lt $exports.Count; ++$index) {
    $entry = $exports[$index]
    $escapedName = $entry.Name.Replace('\', '\\').Replace('"', '\"')
    $headerLines += ('    "{0}",' -f $escapedName)
    $definitionLines += ('    {0}=_BinkProxy{1}@0 @{2}' -f $entry.Name, $index, $entry.Ordinal)
    $functionLines += ('BINK_PROXY({0}, {1})' -f $index, ($index * 4))
}
$headerLines += '};'
$functionLines | Set-Content -LiteralPath $proxyFunctions -Encoding Ascii
$headerLines | Set-Content -LiteralPath $exportsHeader -Encoding Ascii
$definitionLines += ('    ThaiFontFixIsActive=_ThaiFontFixIsActive@0 @{0}' -f $markerOrdinal)
Set-Content -LiteralPath $definition -Value $definitionLines -Encoding Ascii

if ($PrepareOnly) {
    Write-Host "Prepared Bink export definitions from $exportSource in $buildRoot"
    return
}

$verificationTarget = $linkedOutput
if ([string]::IsNullOrWhiteSpace($BuiltDllPath)) {
    $commandLines = @(
        '@echo off',
        'setlocal',
        "call `"$vcvars`" x86 -vcvars_ver=14.16",
        'if errorlevel 1 exit /b %errorlevel%',
        "rc.exe /nologo /fo`"$resource`" `"$resourceSource`"",
        'if errorlevel 1 exit /b %errorlevel%',
        "cl.exe /nologo /c /MT /O2 /W4 /EHsc /DWINVER=0x0501 /D_WIN32_WINNT=0x0501 /I`"$buildRoot`" /Fo`"$proxyObject`" `"$source`"",
        'if errorlevel 1 exit /b %errorlevel%',
        "cl.exe /nologo /c /MT /O2 /W4 /EHsc /DWINVER=0x0501 /D_WIN32_WINNT=0x0501 /DTHAI_FONT_FIX_INTEGRATED=1 /Fo`"$sharedObject`" `"$sharedSource`"",
        'if errorlevel 1 exit /b %errorlevel%',
        "link.exe /nologo /DLL /OUT:`"$linkedOutput`" /DEF:`"$definition`" /MACHINE:X86 /SUBSYSTEM:WINDOWS,5.01 /INCREMENTAL:NO `"$proxyObject`" `"$sharedObject`" `"$resource`" kernel32.lib user32.lib gdi32.lib",
        'if errorlevel 1 exit /b %errorlevel%',
        'endlocal'
    )
    Set-Content -LiteralPath $commandFile -Value $commandLines -Encoding Ascii
    & $env:ComSpec /d /c "`"$commandFile`""
    if ($LASTEXITCODE -ne 0) { throw "MSVC failed to build the Bink proxy (exit code $LASTEXITCODE)." }
} else {
    $verificationTarget = (Resolve-Path -LiteralPath $BuiltDllPath).Path
}
if (-not (Test-Path -LiteralPath $verificationTarget -PathType Leaf)) { throw "Build did not produce $verificationTarget" }

$headers = & $dumpbin /nologo /headers $verificationTarget 2>&1
if ($LASTEXITCODE -ne 0 -or (($headers -join "`n") -notmatch '14C machine \(x86\)') -or
    (($headers -join "`n") -notmatch '5\.01 subsystem version')) {
    throw 'Built Bink proxy did not verify as an x86 Windows XP-compatible PE image.'
}
$builtExports = & $dumpbin /nologo /exports $verificationTarget 2>&1
if ($LASTEXITCODE -ne 0) { throw 'DUMPBIN could not read the built Bink proxy exports.' }
$builtExportText = $builtExports -join "`n"
foreach ($entry in $exports) {
    $escapedName = [regex]::Escape($entry.Name)
    if ($builtExportText -notmatch ("(?m)^\s*" + $entry.Ordinal + '\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+' + $escapedName + '\s*$')) {
        throw "Built proxy did not preserve Bink export '$($entry.Name)' at ordinal $($entry.Ordinal)."
    }
}
if ($builtExportText -notmatch ("(?m)^\s*" + $markerOrdinal + '\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+ThaiFontFixIsActive\s*$')) {
    throw "ThaiFontFixIsActive was not exported at ordinal $markerOrdinal."
}
$imports = & $dumpbin /nologo /imports $verificationTarget 2>&1
if ($LASTEXITCODE -ne 0) { throw 'DUMPBIN could not read the built proxy imports.' }
if (($imports -join "`n") -match '\b(?:Fls(?:Alloc|Free|GetValue|SetValue)|InitializeCriticalSectionEx)\b') {
    throw 'Built Bink proxy imports an API unavailable on Windows XP.'
}

$versionInfo = (Get-Item -LiteralPath $verificationTarget).VersionInfo
if ($versionInfo.FileDescription -ne 'ThaiFontFix BINKW32 Proxy' -or
    $versionInfo.OriginalFilename -ne 'binkw32.dll') {
    throw 'Built DLL metadata does not identify it as the BINKW32 proxy.'
}
Copy-Item -LiteralPath $verificationTarget -Destination $output -Force
Write-Host "Built x86 BINKW32 proxy as binkw32.dll: $output"
Write-Host "Preserved $($exports.Count) original exports with runtime dispatch to binkw32.bin; marker ordinal: $markerOrdinal."
