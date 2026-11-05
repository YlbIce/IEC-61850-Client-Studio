param([string]$PackageRoot = (Join-Path $PSScriptRoot '..\release\0.1.6\Iec61850ClientLite'))
$ErrorActionPreference = 'Stop'
$taskRelease = (Resolve-Path -LiteralPath $PackageRoot).Path
$originalPath = $env:PATH
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$watch = [Diagnostics.Stopwatch]::StartNew()
$ownedProcess = Start-Process -FilePath (Join-Path $taskRelease 'Iec61850ClientLite.exe') -WorkingDirectory $taskRelease -WindowStyle Hidden -PassThru
try {
    $idle = $ownedProcess.WaitForInputIdle(10000)
    $firstIdleMs = $watch.ElapsedMilliseconds
    Start-Sleep -Milliseconds 1500
    $ownedProcess.Refresh()
    if ($ownedProcess.HasExited -or !$idle) { throw 'Packaged UI did not start' }
    $modules = @($ownedProcess.Modules | Where-Object { $_.ModuleName -match '^Qt5|^qwindows|^libgcc|^libstdc|^libwinpthread' } | ForEach-Object { $_.FileName })
    if (!($modules | Where-Object { $_ -like '*\platforms\qwindows.dll' })) { throw 'Qt Windows platform not loaded' }
    foreach ($module in $modules) {
        if (!$module.StartsWith($taskRelease + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "External development DLL loaded: $module" }
    }
    $result = [ordered]@{
        inputIdle = $idle
        firstInputIdleMs = $firstIdleMs
        workingSetMiB = [math]::Round($ownedProcess.WorkingSet64 / 1MB, 1)
        privateBytesMiB = [math]::Round($ownedProcess.PrivateMemorySize64 / 1MB, 1)
        modules = $modules
        note = 'Development machine, disconnected idle window; not a Windows 7 or 4 GB machine benchmark.'
    }
    $result | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot '..\packaged-smoke.json') -Encoding UTF8
    $result | ConvertTo-Json -Depth 4
} finally {
    if (!$ownedProcess.HasExited) { Stop-Process -Id $ownedProcess.Id }
    $env:PATH = $originalPath
}
