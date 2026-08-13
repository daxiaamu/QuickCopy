#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot 'test-driver-verify.txt'
$classPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4D36E96B-E325-11CE-BFC1-08002BE10318}'
$driverPath = Join-Path $env:WINDIR 'System32\drivers\QuickCopyKbd.sys'

$lines = @()
$lines += '=== BCD ==='
$lines += (& bcdedit.exe /enum '{current}' | Select-String -Pattern 'testsigning').Line
$lines += '=== SERVICE ==='
$lines += (& sc.exe qc QuickCopyKbd)
$lines += '=== UPPER FILTERS ==='
$lines += @((Get-ItemProperty -LiteralPath $classPath -Name UpperFilters).UpperFilters)
$lines += '=== SIGNATURE ==='
$signature = Get-AuthenticodeSignature -LiteralPath $driverPath
$lines += "Status=$($signature.Status)"
$lines += "Signer=$($signature.SignerCertificate.Subject)"
$lines += "Driver=$driverPath"
$lines | Set-Content -LiteralPath $output -Encoding UTF8

if ($lines -notcontains 'QuickCopyKbd') { throw 'QuickCopyKbd is missing from keyboard UpperFilters.' }
if (-not (Test-Path -LiteralPath $driverPath)) { throw 'Installed driver file is missing.' }
