#Requires -RunAsAdministrator
param([switch]$SignOnly)
$ErrorActionPreference = 'Stop'
$logPath = Join-Path $PSScriptRoot 'test-driver-setup.log'
Start-Transcript -LiteralPath $logPath -Force | Out-Null

try {
    $secureBoot = $false
    try { $secureBoot = Confirm-SecureBootUEFI } catch {
        if ($_.Exception.Message -notmatch 'not supported') { throw }
    }
    if ($secureBoot) {
        throw 'Secure Boot is enabled. Disable it in UEFI before enabling Windows test signing.'
    }

    $packagePath = Join-Path $PSScriptRoot 'x64\Release'
    $sysPath = Join-Path $packagePath 'QuickCopyKbd.sys'
    $catPath = Join-Path $packagePath 'QuickCopyKbd.cat'
    if (-not (Test-Path -LiteralPath $sysPath) -or -not (Test-Path -LiteralPath $catPath)) {
        throw 'The built SYS/CAT package was not found.'
    }

    $cert = Get-ChildItem Cert:\LocalMachine\My |
        Where-Object Subject -eq 'CN=QuickCopy Test Driver' |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) {
        $cert = New-SelfSignedCertificate -Type CodeSigningCert `
            -Subject 'CN=QuickCopy Test Driver' -CertStoreLocation Cert:\LocalMachine\My `
            -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 `
            -NotAfter (Get-Date).AddYears(2)
    }

    $cerPath = Join-Path $packagePath 'QuickCopyTestDriver.cer'
    Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null
    Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null

    $signTool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' `
        -Recurse -Filter signtool.exe | Where-Object FullName -match '\\x64\\' |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    if (-not $signTool) { throw 'signtool.exe was not found.' }

    & $signTool sign /v /sm /s My /fd SHA256 /sha1 $cert.Thumbprint $sysPath
    if ($LASTEXITCODE -ne 0) { throw 'Failed to sign QuickCopyKbd.sys.' }

    $inf2Cat = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' `
        -Recurse -Filter Inf2Cat.exe | Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $inf2Cat) { throw 'Inf2Cat.exe was not found.' }
    & $inf2Cat /driver:$packagePath /os:10_X64
    if ($LASTEXITCODE -ne 0) { throw 'Failed to regenerate QuickCopyKbd.cat.' }

    & $signTool sign /v /sm /s My /fd SHA256 /sha1 $cert.Thumbprint $catPath
    if ($LASTEXITCODE -ne 0) { throw 'Failed to sign QuickCopyKbd.cat.' }

    if ($SignOnly) {
        Write-Host '[OK] Test driver package signed. No system settings were changed.'
    } else {
        & bcdedit.exe /set testsigning on
        if ($LASTEXITCODE -ne 0) { throw 'Failed to enable Windows test signing.' }
        & (Join-Path $PSScriptRoot 'install_driver.ps1')
        Write-Host '[OK] Test driver prepared. Restart Windows once to activate it.'
    }
} catch {
    Write-Error $_
    exit 1
} finally {
    Stop-Transcript | Out-Null
}
