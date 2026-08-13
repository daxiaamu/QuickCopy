#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'

$classPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4D36E96B-E325-11CE-BFC1-08002BE10318}'
$filters = @((Get-ItemProperty -LiteralPath $classPath -Name UpperFilters -ErrorAction SilentlyContinue).UpperFilters)
$remaining = @($filters | Where-Object { $_ -and $_ -ne 'QuickCopyKbd' })
if ($remaining.Count) {
    Set-ItemProperty -LiteralPath $classPath -Name UpperFilters -Type MultiString -Value $remaining
} else {
    Remove-ItemProperty -LiteralPath $classPath -Name UpperFilters -ErrorAction SilentlyContinue
}

& sc.exe stop QuickCopyKbd *> $null
& sc.exe delete QuickCopyKbd *> $null
Write-Host '[OK] QuickCopyKbd removed from the keyboard filter list. Restart Windows, then delete QuickCopyKbd.sys if it remains.'
