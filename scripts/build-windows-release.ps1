param(
    [string]$Makensis = 'makensis.exe',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\out\release')
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$stage = Join-Path $output 'payload'
New-Item -ItemType Directory -Force $stage | Out-Null
cmake -S $root -B (Join-Path $root 'build') -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build (Join-Path $root 'build') --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'C++ Release build failed' }
ctest --test-dir (Join-Path $root 'build') -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Release tests failed' }
dotnet publish (Join-Path $root 'desktop\gui\TandemAudio.Desktop.csproj') -c Release -r win-x64 `
    --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o $stage
if ($LASTEXITCODE -ne 0) { throw 'Desktop publish failed' }
Copy-Item (Join-Path $root 'build\Release\syncaudio.exe') $stage
Copy-Item (Join-Path $root 'LICENSE') $stage
$installer = Join-Path $output 'TandemAudio-0.14.0-win-x64-setup.exe'
& $Makensis "/DPAYLOAD=$stage" "/DOUTPUT=$installer" (Join-Path $root 'packaging\TandemAudio.nsi')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed' }
Write-Host "Installer: $installer"
