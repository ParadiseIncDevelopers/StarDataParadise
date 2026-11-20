[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Runtime,
    [ValidateSet('tr', 'en')] [string] $Language = 'tr',
    [switch] $Preview
)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../..')).Path
$runtimePath = (Resolve-Path -LiteralPath $Runtime).Path
$output = (New-Item -ItemType Directory -Path (Join-Path $workspace 'out/classic-startup-acceptance') -Force).FullName
$probe = Join-Path $output 'StartupUiProbe.exe'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
& $compiler /nologo /target:exe /platform:x64 /r:System.Windows.Forms.dll /r:System.Drawing.dll "/out:$probe" (Join-Path $PSScriptRoot 'StartupUiProbe.cs')
if ($LASTEXITCODE -ne 0) { throw 'Startup UI probe compilation failed.' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ErrorDialogProbe.exe.config') -Destination ($probe + '.config') -Force
$previousLanguage = $env:SDP_UI_LANGUAGE
$previousLocales = $env:SDP_UI_LOCALES
try {
    $env:SDP_UI_LANGUAGE = $Language
    $env:SDP_UI_LOCALES = Join-Path $runtimePath 'locales'
    $probeArguments = @($runtimePath)
    if ($Preview) { $probeArguments += '--preview' }
    & $probe @probeArguments
    if ($LASTEXITCODE -ne 0) { throw 'Startup UI regression failed.' }
} finally {
    $env:SDP_UI_LANGUAGE = $previousLanguage
    $env:SDP_UI_LOCALES = $previousLocales
}
