$ErrorActionPreference = 'Stop'

$upstream = Join-Path $PSScriptRoot 'ts-ddraw-upstream'
$project = Join-Path $upstream 'ts-ddraw.vcxproj'
$sharedSource = Join-Path (Split-Path -Parent $PSScriptRoot) 'shared\ThaiFontFix.cpp'
$integrationPatch = Join-Path $PSScriptRoot 'patches\ts-ddraw-integration.patch'
$buildRoot = Join-Path $PSScriptRoot 'build-artifacts\msbuild-v143-no-pdb'
$output = Join-Path $PSScriptRoot 'ddraw-integrated.dll'
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$sdkVersion = '10.0.26100.0'
$upstreamCommit = '5c1394b699fd9b6e37ab84b993cab147044b41a9'

foreach ($path in @($project, $sharedSource, $integrationPatch, $msbuild, (Join-Path $upstream 'ddraw.def'), (Join-Path $upstream 'ddraw.rc'))) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required TS-DDraw build path is missing: $path" }
}

$git = Get-Command git -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $git) { throw 'Git is required to prepare the pinned TS-DDraw source checkout.' }
$actualUpstreamCommit = (& $git.Source -C $upstream rev-parse HEAD 2>$null | Select-Object -First 1).Trim()
if ($actualUpstreamCommit -ne $upstreamCommit) {
    throw "TS-DDraw must be checked out at $upstreamCommit (found '$actualUpstreamCommit'). See README.md."
}

# Keep the upstream checkout out of this repository and apply our small integration patch at build time.
$reverseCheck = & $git.Source -C $upstream apply --unidiff-zero --reverse --check $integrationPatch 2>&1
if ($LASTEXITCODE -ne 0) {
    $forwardCheck = & $git.Source -C $upstream apply --unidiff-zero --check $integrationPatch 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "The TS-DDraw integration patch does not apply cleanly: $($forwardCheck -join ' ')"
    }
    & $git.Source -C $upstream apply --unidiff-zero $integrationPatch 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Failed to apply the TS-DDraw integration patch.' }
}

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$arguments = @(
    $project,
    '/t:Rebuild',
    '/p:Configuration=Release',
    '/p:Platform=Win32',
    '/p:PlatformToolset=v143',
    "/p:WindowsTargetPlatformVersion=$sdkVersion",
    '/p:WholeProgramOptimization=false',
    '/p:DebugInformationFormat=None',
    '/p:GenerateDebugInformation=false',
    "/p:OutDir=$buildRoot\",
    "/p:IntDir=$buildRoot\obj\",
    '/m'
)

& $msbuild @arguments
if ($LASTEXITCODE -ne 0) { throw "TS-DDraw MSBuild failed with exit code $LASTEXITCODE" }

$builtDll = Join-Path $buildRoot 'ddraw.dll'
if (-not (Test-Path -LiteralPath $builtDll)) { throw "MSBuild did not produce the expected DLL: $builtDll" }
Copy-Item -LiteralPath $builtDll -Destination $output -Force
$built = Get-Item -LiteralPath $output
Write-Host "Built integrated TS-DDraw with ThaiFontFix using the Visual Studio project: $($built.FullName) ($($built.Length) bytes)"
