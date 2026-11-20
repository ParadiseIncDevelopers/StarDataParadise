# Shared names for canonical builds, runtime seeding and package validation.
$script:SdpArtifactNames = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'artifact-names.json') -Raw | ConvertFrom-Json -AsHashtable

function Get-SdpLegacyArtifactPatterns {
    foreach ($name in $script:SdpArtifactNames.libraries.Keys) { "$name.*" }
    foreach ($name in $script:SdpArtifactNames.files.Keys) { $name }
}

function Copy-SdpBundledClients([string] $From, [string] $To) {
    foreach ($name in @('mysql.exe', 'mysqldump.exe')) {
        $sourceFile = Join-Path $From $name
        $destination = Join-Path $To $script:SdpArtifactNames.files[$name]
        if (!(Test-Path -LiteralPath $sourceFile -PathType Leaf)) { throw "Missing bundled client: $sourceFile" }
        Copy-Item -LiteralPath $sourceFile -Destination $destination -Force
    }
}

function Assert-SdpArtifactNames([string] $Runtime) {
    $runtimePath = (Resolve-Path -LiteralPath $Runtime).Path
    $patterns = @(Get-SdpLegacyArtifactPatterns | ForEach-Object { [regex]::Escape($_).Replace('\*', '.*') })
    $legacyName = [regex]::new('^(?:' + ($patterns -join '|') + ')$', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    $legacy = @(Get-ChildItem -LiteralPath $runtimePath -File -Recurse | Where-Object {
        $legacyName.IsMatch($_.Name)
    })
    if ($legacy.Count) {
        throw "Legacy product artifact names remain. Use a fresh output directory: $($legacy.Name -join ', ')"
    }
    foreach ($directory in $script:SdpArtifactNames.directories.Keys) {
        if (Test-Path -LiteralPath (Join-Path $runtimePath $directory)) { throw "Legacy runtime directory: $directory" }
    }
}
