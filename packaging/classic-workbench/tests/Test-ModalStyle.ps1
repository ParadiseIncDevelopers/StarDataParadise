[CmdletBinding()]
param([Parameter(Mandatory)] [string] $Runtime)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../..')).Path
$runtimePath = (Resolve-Path -LiteralPath $Runtime).Path
$output = (New-Item -ItemType Directory -Path (Join-Path $workspace 'out/classic-modal-acceptance') -Force).FullName
$probe = Join-Path $output 'ModalStyleRuntimeProbe.exe'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
foreach ($name in @('WBControls.dll', 'sdp.utilities.dll', 'base.windows.dll')) {
    Copy-Item -LiteralPath (Join-Path $runtimePath $name) -Destination (Join-Path $output $name) -Force
}
& $compiler /nologo /target:exe /platform:x64 /r:System.Windows.Forms.dll /r:System.Drawing.dll "/r:$(Join-Path $output 'WBControls.dll')" "/out:$probe" (Join-Path $PSScriptRoot 'ModalStyleProbe.cs')
if ($LASTEXITCODE -ne 0) { throw 'Modal style probe compilation failed.' }
& $probe | Tee-Object -FilePath (Join-Path $output 'probe-runtime.log')
if ($LASTEXITCODE -ne 0) { throw 'Packaged modal style checks failed.' }
