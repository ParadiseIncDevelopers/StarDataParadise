param([string]$QtRoot = $env:QTDIR, [string]$NativeArchive = $env:SDP_NATIVE_ARCHIVE, [string]$TestServer, [string]$TestOpenSsl)

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $configure = @('--preset', 'windows-x64')
    if ($QtRoot) { $configure += "-DCMAKE_PREFIX_PATH=$QtRoot" }
    if ($NativeArchive) { $configure += "-DSDP_NATIVE_ARCHIVE=$NativeArchive" }
    if ($TestServer) { $configure += "-DSDP_TEST_SERVER=$TestServer" }
    if ($TestOpenSsl) { $configure += "-DSDP_TEST_OPENSSL=$TestOpenSsl" }
    & cmake @configure
    if ($LASTEXITCODE) { exit $LASTEXITCODE }

    & cmake --build --preset windows-x64
    if ($LASTEXITCODE) { exit $LASTEXITCODE }

    & ctest --preset windows-x64
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
