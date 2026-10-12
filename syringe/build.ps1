param(
    [string]$VisualStudioPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $projectRoot 'shared\ThaiFontFix.cpp'
$definition = Join-Path $PSScriptRoot 'ThaiFontFixSyringe.def'
$resource = Join-Path $PSScriptRoot 'ThaiFontFixSyringe.rc'
$outputRoot = Join-Path $PSScriptRoot 'build-artifacts'
$vcvars = Join-Path $VisualStudioPath 'VC\Auxiliary\Build\vcvars32.bat'

foreach ($required in @($source, $definition, $resource, $vcvars)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required Syringe build path is missing: $required" }
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$targets = @(
    @{ Name = 'ThaiFontFixSyringe-RA2'; Define = 'THAI_FONT_SYRINGE_RA2' },
    @{ Name = 'ThaiFontFixSyringe-YR'; Define = 'THAI_FONT_SYRINGE_YR' }
)

foreach ($target in $targets) {
    $targetRoot = Join-Path $outputRoot $target.Name
    New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
    $dll = Join-Path $targetRoot ($target.Name + '.dll')
    $obj = Join-Path $targetRoot 'ThaiFontFix.obj'
    $res = Join-Path $targetRoot 'ThaiFontFixSyringe.res'
    $headers = Join-Path $targetRoot 'dll-headers.txt'
    $exports = Join-Path $targetRoot 'dll-exports.txt'
    $define = $target.Define
    $command = 'call "' + $vcvars + '" >nul && ' +
        'rc.exe /nologo /D' + $define + ' /fo"' + $res + '" "' + $resource + '" && ' +
        'cl.exe /nologo /LD /O2 /MT /EHsc /DTHAI_FONT_FIX_SYRINGE=1 /D' + $define + '=1 /Fo"' + $obj + '" /Fe"' + $dll + '" "' + $source + '" "' + $res + '" /link /NOLOGO /DEF:"' + $definition + '" /MACHINE:X86 /INCREMENTAL:NO /OUT:"' + $dll + '" user32.lib && ' +
        'dumpbin.exe /nologo /headers "' + $dll + '" > "' + $headers + '" && ' +
        'dumpbin.exe /nologo /exports "' + $dll + '" > "' + $exports + '"'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $($target.Name) with exit code $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $dll)) { throw "Build did not produce expected DLL: $dll" }
    $headerText = Get-Content -LiteralPath $headers -Raw
    if ($headerText -notmatch '14C machine \(x86\)') { throw "Build output is not an x86 DLL: $dll" }
    $exportText = Get-Content -LiteralPath $exports -Raw
    foreach ($exportName in @('SyringeHandshake', 'ThaiFontFixSyringeEntry', 'ThaiFontFixIsActive')) {
        if ($exportText -notmatch ("(?m)^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+" + [regex]::Escape($exportName) + '\s*$')) {
            throw "Build output is missing required Syringe export '$exportName': $dll"
        }
    }
    Write-Host "Built $dll"
}

Write-Host 'Verified x86 images and Syringe exports. The installer copies matching DLLs and .dll.inj files into the game directory.'
