param(
    [string]$Makensis = 'makensis.exe',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\out\release')
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$stage = Join-Path $root 'out\release-payload'
$build = Join-Path $root 'out\build-release'
New-Item -ItemType Directory -Force $stage | Out-Null
cmake -S $root -B $build -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'C++ Release build failed' }
ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Release tests failed' }
dotnet publish (Join-Path $root 'desktop\gui\TandemAudio.Desktop.csproj') -c Release -r win-x64 `
    --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o $stage
if ($LASTEXITCODE -ne 0) { throw 'Desktop publish failed' }
Copy-Item (Join-Path $build 'Release\syncaudio.exe') $stage
Copy-Item (Join-Path $root 'LICENSE') $stage
$installer = Join-Path $output 'TandemAudio-0.14.0-win-x64-setup.exe'
& $Makensis "/DPAYLOAD=$stage" "/DOUTPUT=$installer" (Join-Path $root 'packaging\TandemAudio.nsi')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed' }
Write-Host "Installer: $installer"
