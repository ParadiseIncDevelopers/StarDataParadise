param(
    [Parameter(Mandatory)] [string] $Source,
    [Parameter(Mandatory)] [string] $FrameworkRoot,
    [Parameter(Mandatory)] [string] $FormsOutput,
    [Parameter(Mandatory)] [string] $Output,
    [string] $OracleRuntime = 'C:\Program Files\MySQL\MySQL Workbench 8.0'
)

$ErrorActionPreference = 'Stop'
$Source = (Resolve-Path -LiteralPath $Source).Path
$FrameworkRoot = (Resolve-Path -LiteralPath $FrameworkRoot).Path
$FormsOutput = (Resolve-Path -LiteralPath $FormsOutput).Path
$Output = [IO.Path]::GetFullPath($Output)
$revision = git -C $Source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne 'cea23ea2126ff92a61fe8528bc311d4ab978f61d') {
    throw 'Expected the pinned Workbench 8.0.43 checkout with the StarDataParadise patch applied.'
}
$original = Join-Path $OracleRuntime 'MySQLWorkbench.exe'
if ((Get-Item -LiteralPath $original).VersionInfo.FileVersion -ne '8.0.43' -or
    (Get-AuthenticodeSignature -LiteralPath $original).Status -ne 'Valid') {
    throw 'Expected the signed Oracle Workbench 8.0.43 Windows runtime.'
}
if (Test-Path -LiteralPath $Output) { throw 'Use a new output directory.' }
New-Item -ItemType Directory -Path $Output | Out-Null
robocopy $OracleRuntime $Output /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XF MySQLWorkbench.exe MySQLWorkbench.exe.config *.pyc
if ($LASTEXITCODE -ge 8) { throw "Runtime staging failed ($LASTEXITCODE)." }
foreach ($name in @('mysql.forms.dll', 'mforms.wr.dll')) {
    Copy-Item -LiteralPath (Join-Path $FormsOutput $name) -Destination $Output
}
Copy-Item -LiteralPath (Join-Path $Source 'res/wbdata/main_menu.xml') -Destination (Join-Path $Output 'data')
foreach ($name in @('sidebar_wb.png', 'sidebar_wb@2x.png', 'wb_tile_sakila_dark.png', 'wb_tile_sakila_light.png')) {
    Copy-Item -LiteralPath (Join-Path $Source "images/home/$name") -Destination (Join-Path $Output 'images/home')
}
Get-ChildItem -Path (Join-Path $Source 'images/icons/MySQLWorkbench*') -File |
    Where-Object { $_.Name -match '^MySQLWorkbench(-\d+\.png|\.ico)$' } |
    Copy-Item -Destination (Join-Path $Output 'images/icons')
$plugins = @{
    'wb_utils_grt.py' = 'modules/wb.utils'
    'migration_overview.py' = 'plugins/migration/frontend'
    'migration_summary.py' = 'plugins/migration/frontend'
    'db_copy_overview.py' = 'plugins/migration/dbcopy'
    'wb_admin_config_file_ui.py' = 'plugins/wb.admin/frontend'
    'wb_admin_perfschema.py' = 'plugins/wb.admin/frontend'
}
Get-ChildItem -LiteralPath (Join-Path $Output 'modules') -Recurse -File -Filter '*.py' |
    Where-Object { $plugins.ContainsKey($_.Name) } |
    ForEach-Object {
        $module = Join-Path $Source (Join-Path $plugins[$_.Name] $_.Name)
        Copy-Item -LiteralPath $module -Destination $_.FullName
    }

$frontend = Join-Path $Source 'frontend/windows/MySQLWorkbench'
$project = Join-Path $frontend 'StarDataParadise.csproj'
$document = [xml] (Get-Content -LiteralPath (Join-Path $frontend 'MySQLWorkbench.csproj') -Raw)
$root = $document.DocumentElement
$names = [Xml.XmlNamespaceManager]::new($document.NameTable)
$names.AddNamespace('ms', $root.NamespaceURI)
$root.SelectSingleNode('ms:PropertyGroup/ms:AssemblyName', $names).InnerText = 'StarDataParadise'
foreach ($reference in $root.SelectNodes('//ms:ProjectReference', $names)) {
    $name = $reference.SelectSingleNode('ms:Name', $names).InnerText
    $path = Join-Path $Output "$name.dll"
    if (!(Test-Path -LiteralPath $path)) { throw "Missing Workbench assembly $name." }
    $binary = $document.CreateElement('Reference', $root.NamespaceURI)
    $binary.SetAttribute('Include', $name)
    $hint = $document.CreateElement('HintPath', $root.NamespaceURI)
    $hint.InnerText = $path
    [void] $binary.AppendChild($hint)
    [void] $reference.ParentNode.ReplaceChild($binary, $reference)
}
$hook = $root.SelectSingleNode('//ms:PostBuildEvent', $names)
[void] $hook.ParentNode.RemoveChild($hook)
$document.Save($project)

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' |
    Select-Object -First 1
if (!$msbuild) { throw 'Visual Studio x64 MSBuild is required.' }
$arguments = @($project, '/t:Rebuild', '/p:Configuration=Release_OSS', '/p:Platform=x64',
    "/p:OutputPath=$Output", "/p:TargetFrameworkRootPath=$FrameworkRoot", '/nr:false', '/nologo', '/v:m')
& $msbuild @arguments
exit $LASTEXITCODE
