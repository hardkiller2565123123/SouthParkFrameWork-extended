$ErrorActionPreference = 'Stop'
$target = Join-Path $PSScriptRoot 'third_party\minhook'
$src = Join-Path $target 'src\hook.c'
if (Test-Path $src) { exit 0 }
$zip = Join-Path $env:TEMP 'southpark-minhook-v1.3.3.zip'
$extract = Join-Path $env:TEMP 'southpark-minhook-v1.3.3-extracted'
Write-Host '[Dependencies] Fetching official MinHook v1.3.3 source for static compilation...'
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest 'https://codeload.github.com/TsudaKageyu/minhook/zip/refs/tags/v1.3.3' -OutFile $zip -UseBasicParsing
    if (Test-Path $extract) { Remove-Item -Recurse -Force $extract }
    Expand-Archive -LiteralPath $zip -DestinationPath $extract
    $root = Get-ChildItem -Path $extract -Directory | Select-Object -First 1
    if (-not $root -or -not (Test-Path (Join-Path $root.FullName 'src\hook.c'))) { throw 'Downloaded archive missing MinHook sources.' }
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -Path (Join-Path $root.FullName 'src') -Destination $target -Recurse -Force
    Copy-Item -Path (Join-Path $root.FullName 'include') -Destination $target -Recurse -Force
    Copy-Item -Path (Join-Path $root.FullName 'LICENSE.txt') -Destination $target -ErrorAction SilentlyContinue
    Write-Host '[Dependencies] MinHook source restored; DLL will contain hook implementation.'
} catch {
    Write-Error "Could not fetch MinHook source. Connect to internet and rebuild, or manually place official v1.3.3 src/ and include/ in Framework\third_party\minhook. Details: $_"
    exit 1
} finally {
    Remove-Item $zip -Force -ErrorAction SilentlyContinue
    Remove-Item $extract -Recurse -Force -ErrorAction SilentlyContinue
}
