# QuickCopy keyboard filter

This optional x64 WDM upper-filter driver receives the configured physical keyboard scan code before remote-control software. It signals QuickCopy through a kernel event and suppresses only the matching trigger packet. It does not expose typed keys to user mode.

## Build

Install Visual Studio 2022 with **Desktop development with C++**, then install the matching Windows Driver Kit (WDK). Run:

```bat
build_driver.bat
```

The driver build uses Visual Studio 2022 MSVC together with WDK 10.0.26100 and produces an x64 SYS/CAT package under `x64\\Release`.

## Test installation

Windows x64 will not load an unsigned kernel driver. For development, use a disposable test machine with test signing enabled and a test-signed `QuickCopyKbd.sys`. Then run elevated PowerShell:

```powershell
.\install_driver.ps1
```

Restart Windows after installation. Run `uninstall_driver.ps1` as administrator to remove the class filter, then restart again.

## Production signing

Public distribution requires a properly signed driver package accepted by Windows Code Integrity. Do not distribute instructions asking users to disable driver signature enforcement or test signing.

Keyboard class filters can affect all keyboards. Validate installation, removal, sleep/resume, USB reconnect, and failure recovery in a virtual machine before installing on a primary computer.
