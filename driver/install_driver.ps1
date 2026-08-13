# Run only after QuickCopyKbd.sys has been built and signed (or on a test-signing machine).
#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'

$driverSource = Join-Path $PSScriptRoot 'x64\Release\QuickCopyKbd.sys'
if (-not (Test-Path -LiteralPath $driverSource)) {
    $driverSource = Join-Path $PSScriptRoot 'Release\QuickCopyKbd.sys'
}
if (-not (Test-Path -LiteralPath $driverSource)) {
    throw 'QuickCopyKbd.sys not found. Build the x64 Release driver first.'
}

$driverTarget = Join-Path $env:WINDIR 'System32\drivers\QuickCopyKbd.sys'
Copy-Item -LiteralPath $driverSource -Destination $driverTarget -Force

& sc.exe query QuickCopyKbd *> $null
if ($LASTEXITCODE -ne 0) {
    & sc.exe create QuickCopyKbd type= kernel start= demand error= normal binPath= 'System32\drivers\QuickCopyKbd.sys' DisplayName= 'QuickCopy Keyboard Filter'
    if ($LASTEXITCODE -ne 0) { throw 'Failed to create the QuickCopyKbd service.' }
}

$classPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4D36E96B-E325-11CE-BFC1-08002BE10318}'
$filters = @((Get-ItemProperty -LiteralPath $classPath -Name UpperFilters -ErrorAction SilentlyContinue).UpperFilters)
if ($filters -notcontains 'QuickCopyKbd') {
    $filters += 'QuickCopyKbd'
    Set-ItemProperty -LiteralPath $classPath -Name UpperFilters -Type MultiString -Value $filters
}

Write-Host '[OK] QuickCopyKbd installed. Restart Windows to attach it to the keyboard stack.'
