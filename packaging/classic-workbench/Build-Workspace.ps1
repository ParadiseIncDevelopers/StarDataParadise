[CmdletBinding()]
param(
    [string] $NativeSource = (Join-Path $PSScriptRoot '../../out/mysql-workbench-native-src'),
    [string] $Output = (Join-Path $PSScriptRoot '../../out/classic-workspace-ui'),
    [string] $RuntimeSeed = '',
    [string] $JavaHome = 'C:\Program Files\Java\jdk-17',
    [ValidateRange(1, 8)] [int] $Jobs = 3,
    [string[]] $Projects = @(
        'library/base/base.vcxproj',
        'frontend/windows/WBControls/WBControls.csproj',
        'library/forms/mysql.forms.vcxproj',
        'library/forms/winforms/mforms.wr.vcxproj',
        'backend/wbpublic/wbpublic.be.vcxproj',
        'backend/windows/wbpublic.wr/wbpublic.wr.vcxproj',
        'backend/wbprivate/wbprivate.be.vcxproj',
        'backend/windows/wbprivate.wr/wbprivate.wr.vcxproj',
        'frontend/windows/grt.db/grt.db.fe.csproj',
        'plugins/db.mysql.editors/backend/db.mysql.editors.wbp.be.vcxproj',
        'plugins/db.mysql.editors/windows/wrapper/db.mysql.editors.wbp.wr.vcxproj',
        'plugins/db.mysql.editors/windows/cs/db.mysql.editors.wbp.fe.csproj',
        'plugins/wb.model.editors/backend/wb.model.editors.wbp.be.vcxproj',
        'plugins/wb.model.editors/windows/wrapper/wb.model.editors.wbp.wr.vcxproj',
        'plugins/wb.model.editors/windows/cs/wb.model.editors.wbp.fe.csproj',
        'plugins/db.search/db.search.wbp.be.vcxproj',
        'frontend/windows/sqlide/sqlide.fe.csproj',
        'frontend/windows/MySQLWorkbench/MySQLWorkbench.csproj'
    )
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$canonical = (Resolve-Path -LiteralPath (Join-Path $workspace 'classic-workbench')).Path
$native = (Resolve-Path -LiteralPath $NativeSource).Path
$output = [IO.Path]::GetFullPath($Output)
$outRoot = [IO.Path]::GetFullPath((Join-Path $workspace 'out')) + [IO.Path]::DirectorySeparatorChar
if (!$native.StartsWith($outRoot, [StringComparison]::OrdinalIgnoreCase) -or
    !$output.StartsWith($outRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'NativeSource and Output must be separate directories inside this workspace out/.'
}
if ($output.StartsWith($native + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
    $native.StartsWith($output + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
    $output -eq $native) {
    throw 'Use a separate output directory; the existing native runtime is retained.'
}
if (!$RuntimeSeed) { $RuntimeSeed = Join-Path $native 'bin/x64/Release_OSS' }
$seed = (Resolve-Path -LiteralPath $RuntimeSeed).Path
if ($seed -eq $output) { throw 'RuntimeSeed must differ from Output.' }
foreach ($relative in @('StarDataParadise.exe', 'python312.dll', '_mforms.pyd', 'data/main_menu.xml')) {
    if (!(Test-Path -LiteralPath (Join-Path $seed $relative) -PathType Leaf)) {
        throw "An existing full native build and staged runtime are required. Missing: $relative"
    }
}
git -C $native merge-base --is-ancestor 'dc3590dbfbee29c87e201d4ef53607e5deb0e4f9' HEAD
if ($LASTEXITCODE -ne 0) { throw 'NativeSource must derive from the pinned Workbench 8.0.47 commit.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' |
    Select-Object -First 1
if (!$msbuild) { throw 'Visual Studio x64 MSBuild and the native build dependencies are required.' }
$deps = (Resolve-Path -LiteralPath (Join-Path $workspace 'out/workbench8-deps')).Path
$framework = (Resolve-Path -LiteralPath (Join-Path $workspace 'out/workbench8-tools/framework')).Path
$swig = (Resolve-Path -LiteralPath (Join-Path $workspace 'out/workbench8-tools/swigwin-4.3.1/swig.exe')).Path
$java = (Resolve-Path -LiteralPath (Join-Path $JavaHome 'bin/java.exe')).Path
$logDirectory = (New-Item -ItemType Directory -Path (Join-Path $workspace 'out/classic-workspace-audit') -Force).FullName
$manifestPath = Join-Path $logDirectory 'workspace-source-sync.json'
$oldHashes = @{}
if (Test-Path -LiteralPath $manifestPath) {
    $oldHashes = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -AsHashtable
}

# Copy only canonical source files. Detect unrelated edits before writing any file;
# never mirror/delete an output tree, user settings, or native .git metadata.
$dirty = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$changed = @(& git -C $native -c core.quotepath=false diff HEAD --name-only --)
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect native source changes.' }
$untracked = @(& git -C $native -c core.quotepath=false ls-files --others --exclude-standard)
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect untracked native source files.' }
foreach ($relative in $changed + $untracked) { [void] $dirty.Add($relative.Replace('\', '/')) }
$sourceList = @(& git -C $workspace -c core.quotepath=false ls-files -c -o --exclude-standard -- classic-workbench)
if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate canonical source files.' }
$prefix = 'classic-workbench/'
$plan = [Collections.Generic.List[object]]::new()
$conflicts = [Collections.Generic.List[string]]::new()
$hashes = @{}
foreach ($entry in $sourceList) {
    if (!$entry.StartsWith($prefix, [StringComparison]::Ordinal)) { continue }
    $relative = $entry.Substring($prefix.Length)
    if ($relative -match '(^|/)(\.git|\.vs)(/|$)') { continue }
    $original = Join-Path $canonical $relative
    if (!(Test-Path -LiteralPath $original -PathType Leaf)) { continue }
    $target = Join-Path $native $relative
    $hash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
    $hashes[$relative] = $hash
    $exists = Test-Path -LiteralPath $target -PathType Leaf
    $currentHash = if ($exists) { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash } else { '' }
    if ($hash -eq $currentHash) { continue }
    if ($dirty.Contains($relative) -and (!$exists -or !$oldHashes.ContainsKey($relative) -or $oldHashes[$relative] -ne $currentHash)) {
        $conflicts.Add($relative)
    } else {
        $plan.Add([pscustomobject]@{ Source = $original; Target = $target })
    }
}
if ($conflicts.Count) {
    $conflicts | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDirectory 'workspace-source-conflicts.json') -Encoding UTF8
    throw "Native source edits differ from both canonical source and the last sync. Preserve/reconcile these files first: $($conflicts -join ', ')"
}
foreach ($copy in $plan) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $copy.Target) -Force | Out-Null
    Copy-Item -LiteralPath $copy.Source -Destination $copy.Target -Force
}
$hashes | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

New-Item -ItemType Directory -Path $output -Force | Out-Null
# Seed missing runtime files without replacing a newer workspace build. The
# Visual Studio cache may contain locked files and is excluded at every depth.
robocopy $seed $output /E /XC /XN /XO /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XD .vs .git /XF *.pyc
if ($LASTEXITCODE -ge 8) { throw "Runtime staging failed ($LASTEXITCODE)." }

$drive = @('W', 'V', 'U') | Where-Object { !(Test-Path "${_}:\") } | Select-Object -First 1
if (!$drive) { throw 'No free workspace drive alias.' }
$alias = "${drive}:"
$previousPath = $env:PATH
$previousDeps = $env:WB_3DPARTY_PATH
subst.exe $alias $workspace
if ($LASTEXITCODE -ne 0) { throw 'Build drive alias failed.' }
try {
    $sourceAlias = "$alias\" + [IO.Path]::GetRelativePath($workspace, $native).Replace('/', '\') + '\'
    $outputAlias = "$alias\" + [IO.Path]::GetRelativePath($workspace, $output).Replace('/', '\') + '\'
    $env:WB_3DPARTY_PATH = "$alias\out\workbench8-deps"
    $env:PATH = "$(Split-Path -Parent $swig);$(Split-Path -Parent $java);$previousPath"
    Push-Location $sourceAlias
    try {
        foreach ($project in $Projects) {
            if ($project -match '(^|[\\/])\.\.([\\/]|$)' -or [IO.Path]::IsPathRooted($project)) { throw 'Project paths must be relative to NativeSource.' }
            $projectPath = Join-Path $sourceAlias $project
            if (!(Test-Path -LiteralPath $projectPath -PathType Leaf)) { throw "Missing project: $project" }
            $logName = 'workspace-' + ($project -replace '[^a-zA-Z0-9.-]', '_') + '.log'
            $log = Join-Path $logDirectory $logName
            Write-Output "Building $project"
            & $msbuild $projectPath '/t:Build' '/p:BuildProjectReferences=false' '/p:Configuration=Release_OSS' '/p:Platform=x64' '/p:PlatformToolset=v143' "/p:SolutionDir=$sourceAlias" "/p:OutDir=$outputAlias" "/p:OutputPath=$outputAlias" '/p:PostBuildEvent=' '/p:RunPostBuildEvent=Never' "/p:TargetFrameworkRootPath=$framework" "/m:$Jobs" '/nr:false' '/nologo' '/v:m' *> $log
            if ($LASTEXITCODE -ne 0) {
                Get-Content -LiteralPath $log -Tail 45
                throw "Build failed: $project. Full log: $log"
            }
            Write-Output "PASS $project"
        }
    } finally { Pop-Location }
} finally {
    $env:PATH = $previousPath
    $env:WB_3DPARTY_PATH = $previousDeps
    subst.exe $alias /D
}

function Copy-FlatFiles([string] $From, [string] $To, [string] $Filter) {
    if (!(Test-Path -LiteralPath $From -PathType Container)) { return }
    New-Item -ItemType Directory -Path $To -Force | Out-Null
    foreach ($sourceFile in (Get-ChildItem -LiteralPath $From -Filter $Filter -File)) {
        $destinationFile = Join-Path $To $sourceFile.Name
        # Open native views can retain image handles. Preserve an identical file
        # rather than replacing it; a changed locked resource still fails loudly.
        if ((Test-Path -LiteralPath $destinationFile -PathType Leaf) -and
            (Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash -eq
            (Get-FileHash -LiteralPath $destinationFile -Algorithm SHA256).Hash) { continue }
        Copy-Item -LiteralPath $sourceFile.FullName -Destination $destinationFile -Force
    }
}
function Copy-ResourceTree([string] $From, [string] $To) {
    if (!(Test-Path -LiteralPath $From -PathType Container)) { return }
    New-Item -ItemType Directory -Path $To -Force | Out-Null
    robocopy $From $To /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XD .vs .git /XF *.pyc
    if ($LASTEXITCODE -ge 8) { throw "Resource staging failed: $From ($LASTEXITCODE)" }
}

# Equivalent source locations to modules/PrepareOutputDir.cmd, including newly
# added Python tools. Preserve Python packages rather than flattening them.
$moduleDirectories = @(
    'modules/db.sybase', 'modules/wb.utils', 'modules/wb.sqlide', 'modules/db.mysql',
    'modules/db.mssql', 'modules/db.generic', 'modules/db.sql92', 'modules/db.postgresql',
    'modules/db.sqlanywhere', 'modules/db.sqlite', 'modules/db.msaccess',
    'plugins/wb.admin/frontend', 'plugins/wb.admin/backend', 'plugins/migration',
    'plugins/migration/frontend', 'plugins/migration/backend', 'plugins/migration/dbcopy',
    'plugins/wb.bugreport', 'plugins/wb.query.analysis', 'plugins/wb.updater/backend', 'plugins/wb.sqlide'
)
foreach ($directory in $moduleDirectories) {
    Copy-FlatFiles (Join-Path $canonical $directory) (Join-Path $output 'modules') '*.py'
}
Copy-ResourceTree (Join-Path $canonical 'library/python/workbench') (Join-Path $output 'workbench')
Copy-ResourceTree (Join-Path $canonical 'res/locales') (Join-Path $output 'locales')
Copy-FlatFiles (Join-Path $canonical 'res/wbdata') (Join-Path $output 'data') '*.xml'
Copy-FlatFiles (Join-Path $canonical 'res/wbdata') (Join-Path $output 'data') '*.db'
Copy-FlatFiles (Join-Path $canonical 'res/grtdata') (Join-Path $output 'data') '*.xml'
Copy-FlatFiles (Join-Path $canonical 'res/grt') (Join-Path $output 'structs') 'structs*.xml'
foreach ($directory in @('toolbar', 'changeset', 'admin', 'migration', 'icons')) {
    Copy-FlatFiles (Join-Path $canonical "images/$directory") (Join-Path $output 'images/icons') '*.png'
}
foreach ($directory in @('home', 'sql', 'ui', 'cursors', 'grt')) {
    Copy-ResourceTree (Join-Path $canonical "images/$directory") (Join-Path $output "images/$directory")
}
Copy-FlatFiles (Join-Path $canonical 'images/icons') (Join-Path $output 'images/icons') '*.ico'
Copy-FlatFiles (Join-Path $canonical 'res/mysql.profiles') (Join-Path $output 'mysql.profiles') '*.xml'
Copy-FlatFiles (Join-Path $canonical 'res/snippets') (Join-Path $output 'snippets') '*.txt'
Copy-ResourceTree (Join-Path $canonical 'res/scripts/sys') (Join-Path $output 'sys')
Copy-ResourceTree (Join-Path $canonical 'res/sqlidedata/templates') (Join-Path $output 'modules/data/sqlide')
Copy-ResourceTree (Join-Path $canonical 'res/sqlidedata/context-help') (Join-Path $output 'modules/data/sqlide')
Copy-Item -LiteralPath (Join-Path $canonical 'License.txt') -Destination $output -Force
foreach ($relative in @('StarDataParadise.exe', 'base.dll', 'mysql.forms.dll', 'mforms.wr.dll', 'wbprivate.be.dll', 'python312.dll', '_mforms.pyd', 'modules/sdp_tools_grt.py', 'data/dbquery_toolbar.xml')) {
    if (!(Test-Path -LiteralPath (Join-Path $output $relative) -PathType Leaf)) { throw "Incomplete workspace runtime: $relative" }
}
[pscustomobject]@{ Executable = (Join-Path $output 'StarDataParadise.exe'); SourcesUpdated = $plan.Count; Logs = $logDirectory }
