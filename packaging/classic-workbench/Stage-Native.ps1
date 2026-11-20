param(
    [string] $Source = (Join-Path $PSScriptRoot '../../out/mysql-workbench-native-src'),
    [string] $Dependencies = (Join-Path $PSScriptRoot '../../out/workbench8-deps'),
    [string] $BuildOutput = '',
    [string] $Output = (Join-Path $PSScriptRoot '../../out/classic-native-sdp-ui')
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Artifact-Names.ps1')
$source = (Resolve-Path -LiteralPath $Source).Path
$deps = (Resolve-Path -LiteralPath $Dependencies).Path
$workspace = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if (!$BuildOutput) { $BuildOutput = Join-Path $source 'bin/x64/Release_OSS' }
$buildOutput = (Resolve-Path -LiteralPath $BuildOutput).Path
$package = [IO.Path]::GetFullPath($Output)
$outRoot = [IO.Path]::GetFullPath((Join-Path $workspace 'out')) + [IO.Path]::DirectorySeparatorChar
if (!$package.StartsWith($outRoot, [StringComparison]::OrdinalIgnoreCase) -or $package -eq $buildOutput -or
    $package.StartsWith($buildOutput + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
    $buildOutput.StartsWith($package + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output must be a separate directory inside this workspace out/.'
}
if (Test-Path -LiteralPath $package) { throw 'Use a fresh output directory; existing runtimes are retained.' }
New-Item -ItemType Directory -Path $package | Out-Null
$legacyPatterns = @(Get-SdpLegacyArtifactPatterns)
robocopy $buildOutput $package /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XD .vs .git mysql.profiles /XF *.pyc WBTests.exe StarDataParadiseModelAcceptance.exe @legacyPatterns
if ($LASTEXITCODE -ge 8) { throw "Runtime staging failed ($LASTEXITCODE)." }
$exe = Get-Item -LiteralPath (Join-Path $package 'StarDataParadise.exe')
if ($exe.VersionInfo.FileVersion -notin '8.0.47', '8.0.47.0') {
    throw "Unexpected Workbench file version: $($exe.VersionInfo.FileVersion)"
}

# The solution stages the Workbench modules. Complete their dependency set with
# the current runtime library names and the matching Python and spatial data.
Get-ChildItem -LiteralPath "$deps/lib" -Filter '*.dll' -File |
    Where-Object { $_.Name -notin @('cairo-script-interpreter-2.dll', 'pkgconf-8.dll') } |
    Copy-Item -Destination $package -Force
Get-ChildItem -LiteralPath "$deps/Python/DLLs" -Filter '*.dll' -File |
    Copy-Item -Destination "$package/python/DLLs" -Force
Copy-Item -LiteralPath "$source/License.txt" -Destination $package -Force
Copy-Item -LiteralPath "$deps/licenses" -Destination $package -Recurse -Force
Copy-SdpBundledClients (Join-Path $deps 'bin') $package
$share = New-Item -Path "$package/share" -ItemType Directory -Force
Copy-Item -LiteralPath "$deps/share/gdal", "$deps/share/proj" -Destination $share.FullName -Recurse -Force

$needed = @('base.dll', 'grt.dll', 'sdp.forms.dll', 'mforms.wr.dll', 'Scintilla.dll',
    'mforms.py', '_mforms.pyd', 'cairo.py', '_cairo.pyd', 'python312.dll', 'python/lib/encodings/__init__.py',
    'data/main_menu.xml', 'images/home/sidebar_wb.png', 'images/icons/sdpWorkbench-128.png',
    'sdp.exe', 'sdpdump.exe', 'ogrinfo.exe', 'ogr2ogr.exe')
$missing = @($needed | Where-Object { !(Test-Path -LiteralPath (Join-Path $package $_) -PathType Leaf) })
if ($missing.Count) { throw "Incomplete Workbench package: $($missing -join ', ')" }

Push-Location $package
try {
    foreach ($tool in 'ogrinfo.exe', 'ogr2ogr.exe') {
        & ".\$tool" --version
        if ($LASTEXITCODE -ne 0) { throw "$tool failed to start ($LASTEXITCODE)" }
    }
} finally {
    Pop-Location
}

$exe.VersionInfo | Select-Object FileVersion, ProductName, FileDescription
Assert-SdpArtifactNames $package
$global:LASTEXITCODE = 0
