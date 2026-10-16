[CmdletBinding()]
param(
    [string]$BuildDir = 'build/windows-release',
    [ValidateSet('Release', 'RelWithDebInfo')][string]$Config = 'Release',
    [string]$QtBinDir,
    [string[]]$RuntimeDirectories = @()
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'Packaging requires native Windows PowerShell 7 and an MSVC x64 developer shell.' }

$repo = Split-Path $PSScriptRoot -Parent
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { [IO.Path]::GetFullPath($BuildDir) } else { [IO.Path]::GetFullPath((Join-Path $repo $BuildDir)) }
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repo 'build')) + [IO.Path]::DirectorySeparatorChar
if (-not $build.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDir must be a child of this worktree build directory.'
}
function Assert-UnlinkedPath([string]$Path) {
    $cursor = [IO.Path]::GetFullPath($Path)
    while ($cursor.StartsWith($repo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        if ((Test-Path $cursor) -and ((Get-Item $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Refusing a linked packaging path: $cursor"
        }
        $cursor = Split-Path $cursor -Parent
    }
}
Assert-UnlinkedPath $build
if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) { throw 'Configure a Release x64 CMake build first.' }

function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}
function Get-CacheValue([string]$Name) {
    $line = Get-Content (Join-Path $build 'CMakeCache.txt') | Where-Object { $_ -match "^${Name}:[^=]+=" } | Select-Object -First 1
    if ($line) { return ($line -split '=', 2)[1] }
    return ''
}
if ((Get-CacheValue 'CMAKE_SIZEOF_VOID_P') -eq '4') { throw 'Only x64 packages are supported.' }
if (-not $QtBinDir) {
    $qtCmake = Get-CacheValue 'Qt6_DIR'
    if (-not $qtCmake) { throw 'Qt6_DIR not found; provide -QtBinDir.' }
    $QtBinDir = [IO.Path]::GetFullPath((Join-Path $qtCmake '../../../bin'))
}
$deploy = Join-Path $QtBinDir 'windeployqt.exe'
if (-not (Test-Path $deploy)) { throw "windeployqt not found: $deploy" }
if (-not $env:VCToolsRedistDir) { throw 'Use an MSVC developer shell providing VCToolsRedistDir.' }
$crt = @(Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Sort-Object Name)
if ($crt.Count -ne 1) { throw 'Expected one licensed x64 Microsoft.VC*.CRT redistributable directory.' }

# Every entry point, including the CMake package target, rebuilds and runs tests.
Invoke-Checked 'cmake' @('--build', $build, '--config', $Config, '--parallel', '2')
$oldPath = $env:PATH
$oldPlatform = $env:QT_QPA_PLATFORM
try {
    $env:PATH = (@($QtBinDir) + $RuntimeDirectories + @($oldPath)) -join ';'
    $env:QT_QPA_PLATFORM = 'offscreen'
    Invoke-Checked 'ctest' @('--test-dir', $build, '-C', $Config, '--output-on-failure', '--no-tests=error')
} finally {
    $env:PATH = $oldPath
    $env:QT_QPA_PLATFORM = $oldPlatform
}

$stage = Join-Path $build 'windows-package-stage'
Assert-UnlinkedPath $stage
if (Test-Path $stage) {
    if (Get-ChildItem $stage -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1) {
        throw 'Refusing to remove a staging tree containing links.'
    }
    Remove-Item $stage -Recurse -Force
}
Invoke-Checked 'cmake' @('--install', $build, '--config', $Config, '--prefix', $stage)
$bin = Join-Path $stage 'bin'
$exe = Join-Path $bin 'prismatic-launch.exe'
if (-not (Test-Path $exe)) { throw 'CMake install did not produce bin/prismatic-launch.exe.' }
# Copy only the redistributable CRT; never copy compiler/tool DLLs from System32.
Copy-Item (Join-Path $crt[0].FullName '*.dll') $bin
Invoke-Checked $deploy @('--release', '--no-compiler-runtime', '--no-translations', '--dir', $bin, $exe)
foreach ($required in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'vcruntime140.dll', 'msvcp140.dll', 'platforms/qwindows.dll')) {
    if (-not (Test-Path (Join-Path $bin $required))) { throw "Deployment omitted $required" }
}
Set-Content (Join-Path $bin 'qt.conf') "[Paths]`nPrefix=.`nPlugins=." -Encoding utf8
$search = @($bin, $QtBinDir, (Join-Path $build 'vcpkg_installed/x64-windows/bin'), (Join-Path $repo 'vcpkg_installed/x64-windows/bin'), (Join-Path $repo 'build/windows-dependencies/x64-windows/bin'))
$search += $RuntimeDirectories
$search += @($oldPath -split ';')
$search = @($search | Where-Object { $_ -and (Test-Path $_ -PathType Container) } | Select-Object -Unique)
$runtimeScript = Join-Path $repo 'packaging/windows/runtime-dependencies.cmake'
Invoke-Checked 'cmake' @("-DPACKAGE_BIN=$bin", '-DMODE=COPY', "-DSEARCH_DIRECTORIES=$($search -join '|')", '-P', $runtimeScript)
Invoke-Checked 'cmake' @("-DPACKAGE_BIN=$bin", '-DMODE=VERIFY', '-P', $runtimeScript)
if (-not (Get-ChildItem $bin -Filter '*archive*.dll')) { throw 'LibArchive runtime DLL was not deployed.' }

# Read the PE header directly so architecture checks do not depend on tool locale.
foreach ($file in @(Get-ChildItem $bin -Recurse -File | Where-Object { $_.Extension -in '.dll', '.exe' })) {
    $stream = [IO.File]::OpenRead($file.FullName)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid PE file: $($file.Name)" }
        $stream.Position = 0x3c
        $stream.Position = $reader.ReadUInt32()
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "Non-x64 PE file: $($file.Name)"
        }
    } finally { $reader.Dispose() }
}

# The staged program must load Qt and its platform plugin without SDK PATH entries.
$oldPluginPath = $env:QT_PLUGIN_PATH
$oldPlatformPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = $null
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
    $env:QT_QPA_PLATFORM = 'windows'
    foreach ($argument in @('--help', '--version')) {
        $process = Start-Process $exe -ArgumentList $argument -WorkingDirectory $build -PassThru
        if (-not $process.WaitForExit(20000)) { $process.Kill(); throw "Packaged $argument smoke test timed out." }
        if ($process.ExitCode -ne 0) { throw "Packaged $argument smoke test failed: $($process.ExitCode)" }
    }
} finally {
    $env:PATH = $oldPath
    $env:QT_PLUGIN_PATH = $oldPluginPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldPlatformPath
    $env:QT_QPA_PLATFORM = $oldPlatform
}

$licenses = Join-Path $stage 'licenses'
New-Item $licenses -ItemType Directory -Force | Out-Null
Copy-Item (Join-Path $repo 'docs/packaging-windows.md') (Join-Path $stage 'README-Windows.md')
foreach ($share in @((Join-Path $build 'vcpkg_installed/x64-windows/share'), (Join-Path $repo 'vcpkg_installed/x64-windows/share'), (Join-Path $repo 'build/windows-dependencies/x64-windows/share'))) {
    if (Test-Path $share) {
        foreach ($copyright in @(Get-ChildItem $share -Filter copyright -Recurse -File)) {
            Copy-Item $copyright.FullName (Join-Path $licenses ($copyright.Directory.Name + '-copyright.txt'))
        }
    }
}
$qtRoot = Split-Path $QtBinDir -Parent
foreach ($licenseDir in @('Licenses', 'LICENSES')) {
    if (Test-Path (Join-Path $qtRoot $licenseDir)) { Copy-Item (Join-Path $qtRoot $licenseDir) (Join-Path $licenses 'Qt') -Recurse -Force; break }
}
foreach ($licenseFile in @(Get-ChildItem $repo -File -Filter 'LICENSE*')) { Copy-Item $licenseFile.FullName $licenses }
$manifest = @(Get-ChildItem $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = [IO.Path]::GetRelativePath($stage, $_.FullName).Replace('\', '/'); sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$manifest | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $stage 'runtime-manifest.json') -Encoding utf8

$version = Get-CacheValue 'CMAKE_PROJECT_VERSION'
if (-not $version -or $version -notmatch '^\d+\.\d+\.\d+([-.][A-Za-z0-9.]+)?$') { throw 'CMake project version is missing or unsafe.' }
$outputDir = Join-Path $repo 'build/artifacts/windows'
Assert-UnlinkedPath $outputDir
New-Item $outputDir -ItemType Directory -Force | Out-Null
$archivePath = Join-Path $outputDir "PrismaticLauncher-$version-windows-x64.zip"
if (Test-Path $archivePath) { Remove-Item $archivePath -Force }
Add-Type -AssemblyName System.IO.Compression
$epoch = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
if ($env:SOURCE_DATE_EPOCH) { $epoch = [DateTimeOffset]::FromUnixTimeSeconds([long]$env:SOURCE_DATE_EPOCH) }
if ($epoch.Year -lt 1980 -or $epoch.Year -gt 2107) { throw 'SOURCE_DATE_EPOCH is outside ZIP timestamp range.' }
$zip = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in @(Get-ChildItem $stage -Recurse -File | Sort-Object FullName)) {
        $relative = [IO.Path]::GetRelativePath($stage, $file.FullName).Replace('\', '/')
        $entry = $zip.CreateEntry("PrismaticLauncher/$relative", [IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = $epoch
        $inputStream = [IO.File]::OpenRead($file.FullName)
        $outputStream = $entry.Open()
        try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose(); $inputStream.Dispose() }
    }
} finally { $zip.Dispose() }
$checksum = (Get-FileHash $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content "$archivePath.sha256" "$checksum  $([IO.Path]::GetFileName($archivePath))" -Encoding ascii
Write-Host "Verified Windows x64 package: $archivePath"
