param(
    [string] $Target = 'Build',
    [string] $JavaHome = 'C:\Program Files\Java\jdk-17',
    [ValidateRange(1, 8)] [int] $Jobs = 3
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$source = Join-Path $workspace 'out/mysql-workbench-native-src'
git -C $source merge-base --is-ancestor 'dc3590dbfbee29c87e201d4ef53607e5deb0e4f9' HEAD
if ($LASTEXITCODE -ne 0) {
    throw 'Expected a Workbench 8.0.47 checkout based on the pinned upstream commit.'
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' |
    Select-Object -First 1
if (!$msbuild) { throw 'Visual Studio x64 MSBuild is required.' }

$java = (Resolve-Path -LiteralPath (Join-Path $JavaHome 'bin/java.exe')).Path
$logDirectory = New-Item -Path (Join-Path $workspace 'out/mysql-migration') -ItemType Directory -Force
$log = Join-Path $logDirectory.FullName 'workbench-8.0.47-native-build.log'
$drive = @('W', 'V', 'U') | Where-Object { !(Test-Path "${_}:\") } | Select-Object -First 1
if (!$drive) { throw 'No free drive letter for the upstream Workbench build batch files.' }
$alias = "${drive}:"
$originalPath = $env:PATH
$originalDependencyPath = $env:WB_3DPARTY_PATH
subst.exe $alias $workspace
if ($LASTEXITCODE -ne 0) { throw 'Could not map the workspace to a path without spaces.' }
try {
    $out = "$alias\out"
    $env:WB_3DPARTY_PATH = "$out\workbench8-deps"
    $swig = (Resolve-Path -LiteralPath "$out\workbench8-tools\swigwin-4.3.1\swig.exe").Path
    $env:PATH = "$(Split-Path -Parent $swig);$(Split-Path -Parent $java);$originalPath"
    $framework = "$out\workbench8-tools\framework"
    $project = "$out\mysql-workbench-native-src\MySQLWorkbench.sln"
    $arguments = @($project, "/t:$Target", '/p:Configuration=Release_OSS', '/p:Platform=x64',
        '/p:PlatformToolset=v143', "/p:TargetFrameworkRootPath=$framework", "/m:$Jobs",
        '/nr:false', '/nologo', '/v:m', "/flp:logfile=$log;verbosity=normal")

    Push-Location (Split-Path -Parent $project)
    try {
        & $msbuild @arguments
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
} finally {
    $env:PATH = $originalPath
    $env:WB_3DPARTY_PATH = $originalDependencyPath
    subst.exe $alias /D
}
exit $code
