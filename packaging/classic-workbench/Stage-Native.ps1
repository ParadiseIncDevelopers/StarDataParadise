param(
    [string] $Source = (Join-Path $PSScriptRoot '../../out/mysql-workbench-native-src'),
    [string] $Dependencies = (Join-Path $PSScriptRoot '../../out/workbench8-deps')
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $Source).Path
$deps = (Resolve-Path -LiteralPath $Dependencies).Path
$package = Join-Path $source 'bin/x64/Release_OSS'
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
$share = New-Item -Path "$package/share" -ItemType Directory -Force
Copy-Item -LiteralPath "$deps/share/gdal", "$deps/share/proj" -Destination $share.FullName -Recurse -Force

$needed = @('base.dll', 'grt.dll', 'mysql.forms.dll', 'mforms.wr.dll', 'Scintilla.dll',
    'mforms.py', '_mforms.pyd', 'cairo.py', '_cairo.pyd', 'python312.dll', 'python/lib/encodings/__init__.py',
    'data/main_menu.xml', 'images/home/sidebar_wb.png', 'images/icons/MySQLWorkbench-128.png',
    'mysql.exe', 'mysqldump.exe', 'ogrinfo.exe', 'ogr2ogr.exe')
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
