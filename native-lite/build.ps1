param(
    [string]$QtRoot = 'D:\Qt\5.15.2\mingw81_64',
    [string]$MingwRoot = 'D:\Qt\Tools\mingw810_64',
    [string]$Ninja = 'D:\Qt\Tools\Ninja\ninja.exe',
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$env:PATH = "$MingwRoot\bin;$QtRoot\bin;" + $env:PATH
$taskRoot = $PSScriptRoot
$buildRoot = Join-Path $taskRoot 'build'
cmake -S $taskRoot -B $buildRoot -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_C_COMPILER=$MingwRoot/bin/gcc.exe" "-DCMAKE_CXX_COMPILER=$MingwRoot/bin/g++.exe" "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'CMake configure failed' }
cmake --build $buildRoot --target Iec61850ClientLite lite_tests -j 4
if ($LASTEXITCODE) { throw 'Build failed' }
if ($Package) {
    $releaseRoot = Join-Path $taskRoot 'release\0.1.6'
    $packageRoot = Join-Path $releaseRoot 'Iec61850ClientLite'
    New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildRoot 'Iec61850ClientLite.exe') -Destination $packageRoot
    # Explicit dependency manifest for this raster Widgets application. Qt 5.15.2
    # windeployqt misclassifies this MinGW kit's unstripped plugins as debug.
    # Copy matching kit DLLs; do not fall back to another installed Qt version.
    foreach ($dll in @('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll')) {
        Copy-Item -LiteralPath (Join-Path $QtRoot "bin\$dll") -Destination $packageRoot
    }
    foreach ($plugin in @('platforms\qwindows.dll', 'styles\qwindowsvistastyle.dll')) {
        $pluginTarget = Join-Path $packageRoot $plugin
        New-Item -ItemType Directory -Force -Path (Split-Path $pluginTarget) | Out-Null
        Copy-Item -LiteralPath (Join-Path $QtRoot "plugins\$plugin") -Destination $pluginTarget
    }
    Set-Content -LiteralPath (Join-Path $packageRoot 'qt.conf') -Value "[Paths]`nPrefix=.`nPlugins=." -Encoding ASCII
    New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot 'translations') | Out-Null
    Copy-Item -LiteralPath (Join-Path $QtRoot 'translations\qt_zh_CN.qm') -Destination (Join-Path $packageRoot 'translations')
    foreach ($dll in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
        Copy-Item -LiteralPath (Join-Path $MingwRoot "bin\$dll") -Destination $packageRoot
    }
    $licenses = Join-Path $packageRoot 'licenses'
    New-Item -ItemType Directory -Force -Path $licenses | Out-Null
    Copy-Item -LiteralPath (Join-Path $taskRoot '..\third_party\libiec61850\COPYING') -Destination (Join-Path $licenses 'libiec61850-GPL-3.0.txt')
    Copy-Item -LiteralPath (Join-Path $taskRoot 'README.md') -Destination $packageRoot
    New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot 'profiles') | Out-Null
    Copy-Item -LiteralPath (Join-Path $taskRoot 'profiles\atc61850-local1.json') -Destination (Join-Path $packageRoot 'profiles')
    New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot 'docs') | Out-Null
    Copy-Item -LiteralPath (Join-Path $taskRoot 'docs\ARCHITECTURE.md') -Destination (Join-Path $packageRoot 'docs')
    Copy-Item -LiteralPath (Join-Path $taskRoot 'docs\BACKGROUND-REPORTS-0.1.6.md') -Destination (Join-Path $packageRoot 'docs')
    Copy-Item -LiteralPath (Join-Path $taskRoot 'docs\HISTORY-0.1.5.md') -Destination (Join-Path $packageRoot 'docs')
    Copy-Item -LiteralPath (Join-Path $taskRoot 'docs\CONNECTION-0.1.4.md') -Destination (Join-Path $packageRoot 'docs')
    Copy-Item -LiteralPath (Join-Path $taskRoot 'THIRD-PARTY.md') -Destination $licenses
    Get-ChildItem (Join-Path $taskRoot 'licenses') -File | Copy-Item -Destination $licenses
    $archive = Join-Path $releaseRoot 'IEC61850-Client-Lite-0.1.6-Win7-x64.zip'
    Compress-Archive -Path $packageRoot -DestinationPath $archive -Force
    Get-FileHash -LiteralPath $archive -Algorithm SHA256
}
