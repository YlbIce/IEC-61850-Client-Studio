param([switch]$Fix)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot
$files = Get-ChildItem (Join-Path $taskRoot 'src'), (Join-Path $taskRoot 'tests'), $PSScriptRoot -File |
    Where-Object { $_.Extension -in '.cpp', '.h' }
foreach ($file in $files) {
    if ($Fix) { & clang-format -i $file.FullName }
    else { & clang-format --dry-run --Werror $file.FullName }
    if ($LASTEXITCODE) { throw "Format check failed: $($file.FullName)" }
}
Write-Output "C++ formatting verified: $($files.Count) files"
