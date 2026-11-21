[CmdletBinding()]
param(
    [string] $NativeSource = (Join-Path $PSScriptRoot '../../out/mysql-workbench-native-src'),
    [string] $Output = (Join-Path $PSScriptRoot '../../out/classic-sdp-names-ui'),
    [string] $RuntimeSeed = '',
    [switch] $SyncOnly,
    [string] $JavaHome = 'C:\Program Files\Java\jdk-17',
    [ValidateRange(1, 8)] [int] $Jobs = 3,
    [string[]] $Projects = @(
        'library/base/base.vcxproj',
        'library/grt/grt.vcxproj',
        'library/mysql.canvas/sdp.canvas.vcxproj',
        'library/parsers/parsers.vcxproj',
        'library/forms/sdp.forms.vcxproj',
        'library/cdbc/cdbc.vcxproj',
        'library/base.wr/base.wr.vcxproj',
        'library/base.windows/base.windows.csproj',
        'frontend/windows/WBControls/WBControls.csproj',
        'library/forms/winforms/mforms.wr.vcxproj',
        'backend/wbpublic/wbpublic.be.vcxproj',
        'backend/windows/wbpublic.wr/wbpublic.wr.vcxproj',
        'frontend/windows/MySQLCsUtilities/sdp.utilities.csproj',
        'backend/wbprivate/wbprivate.be.vcxproj',
        'backend/windows/wbprivate.wr/wbprivate.wr.vcxproj',
        'frontend/windows/WBPlugins/WBPlugins.csproj',
        'frontend/windows/sqlide/sqlide.fe.csproj',
        'frontend/windows/grt.db/grt.db.fe.csproj',
        'modules/db.mysql/db.sdp.grt.vcxproj',
        'plugins/db.mysql.editors/backend/db.sdp.editors.wbp.be.vcxproj',
        'plugins/db.mysql.editors/windows/wrapper/db.sdp.editors.wbp.wr.vcxproj',
        'plugins/db.mysql.editors/windows/cs/db.sdp.editors.wbp.fe.csproj',
        'plugins/wb.model.editors/backend/wb.model.editors.wbp.be.vcxproj',
        'plugins/wb.model.editors/windows/wrapper/wb.model.editors.wbp.wr.vcxproj',
        'plugins/wb.model.editors/windows/cs/wb.model.editors.wbp.fe.csproj',
        'plugins/wb.printing/backend/wb.printing.wbp.be.vcxproj',
        'plugins/wb.printing/windows/wrapper/wb.printing.wbp.wr.vcxproj',
        'plugins/wb.printing/windows/cs/wb.printing.wbp.fe.csproj',
        'plugins/db.search/db.search.wbp.be.vcxproj',
        'modules/db.mysql.query/db.sdp.query.grt.vcxproj',
        'modules/db.mysql.parser/db.sdp.parser.grt.vcxproj',
        'modules/db.mysql.sqlparser/db.sdp.sqlparser.grt.vcxproj',
        'plugins/db.mysql/db.sdp.wbp.vcxproj',
        'plugins/db.mysql.diff.reporting/db.sdp.diff.reporting.wbp.vcxproj',
        'modules/wb.model/wb.model.grt.vcxproj',
        'plugins/wb.model.snippets/wb.model.snippets.wbp.vcxproj',
        'modules/utilities/utilities.grt.vcxproj',
        'library/forms/swig/_mforms.vcxproj',
        'library/forms/swig/_cairo.vcxproj',
        'frontend/windows/MySQLWorkbench/StarDataParadise.csproj'
    )
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Artifact-Names.ps1')
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
if (!$SyncOnly) {
    if (!$RuntimeSeed) { $RuntimeSeed = Join-Path $native 'bin/x64/Release_OSS' }
    $seed = (Resolve-Path -LiteralPath $RuntimeSeed).Path
    if ($seed -eq $output) { throw 'RuntimeSeed must differ from Output.' }
    foreach ($relative in @('StarDataParadise.exe', 'python312.dll', '_mforms.pyd', 'data/main_menu.xml')) {
        if (!(Test-Path -LiteralPath (Join-Path $seed $relative) -PathType Leaf)) {
            throw "An existing full native build and staged runtime are required. Missing: $relative"
        }
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
if ($SyncOnly) {
    $global:LASTEXITCODE = 0
    [pscustomobject]@{ NativeSource = $native; SourcesUpdated = $plan.Count; Manifest = $manifestPath }
    return
}

New-Item -ItemType Directory -Path $output -Force | Out-Null
# Seed missing runtime files without replacing a newer workspace build. The
# Visual Studio cache may contain locked files and is excluded at every depth.
$legacyPatterns = @(Get-SdpLegacyArtifactPatterns)
robocopy $seed $output /E /XC /XN /XO /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XD .vs .git mysql.profiles /XF *.pyc WBTests.exe StarDataParadiseModelAcceptance.exe @legacyPatterns
if ($LASTEXITCODE -ge 8) { throw "Runtime staging failed ($LASTEXITCODE)." }

$drive = @('W', 'V', 'U') | Where-Object { !(Test-Path "${_}:\") } | Select-Object -First 1
if (!$drive) { throw 'No free workspace drive alias.' }
$alias = "${drive}:"
$previousPath = $env:PATH
$previousDeps = $env:WB_3DPARTY_PATH
$previousJavaExecutable = $env:SDP_JAVA_EXECUTABLE
subst.exe $alias $workspace
if ($LASTEXITCODE -ne 0) { throw 'Build drive alias failed.' }
try {
    $sourceAlias = "$alias\" + [IO.Path]::GetRelativePath($workspace, $native).Replace('/', '\') + '\'
    $outputAlias = "$alias\" + [IO.Path]::GetRelativePath($workspace, $output).Replace('/', '\') + '\'
    $env:WB_3DPARTY_PATH = "$alias\out\workbench8-deps"
    $env:SDP_JAVA_EXECUTABLE = $java
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
            & $msbuild $projectPath '/t:Build' '/p:BuildProjectReferences=false' '/p:Configuration=Release_OSS' '/p:Platform=x64' '/p:PlatformToolset=v143' "/p:SolutionDir=$sourceAlias" "/p:OutDir=$outputAlias" "/p:OutputPath=$outputAlias" '/p:PostBuildEvent=' '/p:RunPostBuildEvent=Never' "/p:TargetFrameworkRootPath=$framework" "/p:SdpSwigExecutable=$swig" "/m:$Jobs" '/nr:false' '/nologo' '/v:m' *> $log
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
    $env:SDP_JAVA_EXECUTABLE = $previousJavaExecutable
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
Copy-FlatFiles (Join-Path $canonical 'modules/db.mysql/res') (Join-Path $output 'modules/data') '*.xml'
Copy-FlatFiles (Join-Path $canonical 'res/scripts/python') $output '*.py'
Copy-FlatFiles (Join-Path $canonical 'res/scripts/vbs') $output '*.vbs'
Copy-FlatFiles (Join-Path $canonical 'res/scripts/shell') $output '*.vbs'
Copy-SdpBundledClients (Join-Path $deps 'bin') $output
foreach ($wrapper in @('mforms.py', 'cairo.py')) {
    Copy-FlatFiles (Join-Path $native 'library/forms/swig') $output $wrapper
}
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
Copy-Item -LiteralPath (Join-Path $canonical 'images/icons/StarDataParadiseDoc.ico') -Destination $output -Force
Copy-FlatFiles (Join-Path $canonical 'res/sdp.profiles') (Join-Path $output 'sdp.profiles') '*.xml'
Copy-FlatFiles (Join-Path $canonical 'res/snippets') (Join-Path $output 'snippets') '*.txt'
Copy-ResourceTree (Join-Path $canonical 'res/scripts/sys') (Join-Path $output 'sys')
Copy-ResourceTree (Join-Path $canonical 'res/sqlidedata/templates') (Join-Path $output 'modules/data/sqlide')
Copy-ResourceTree (Join-Path $canonical 'res/sqlidedata/context-help') (Join-Path $output 'modules/data/sqlide')
Copy-Item -LiteralPath (Join-Path $canonical 'License.txt') -Destination $output -Force
foreach ($relative in @('StarDataParadise.exe', 'base.dll', 'sdp.forms.dll', 'mforms.wr.dll', 'wbprivate.be.dll', 'python312.dll', '_mforms.pyd', 'modules/sdp_tools_grt.py', 'data/dbquery_toolbar.xml')) {
    if (!(Test-Path -LiteralPath (Join-Path $output $relative) -PathType Leaf)) { throw "Incomplete workspace runtime: $relative" }
}
Assert-SdpArtifactNames $output
$global:LASTEXITCODE = 0
[pscustomobject]@{ Executable = (Join-Path $output 'StarDataParadise.exe'); SourcesUpdated = $plan.Count; Logs = $logDirectory }
