# Project Tools

Project-owned host tools, probe firmware configuration, build helpers, and test utilities live here.

Manufacturer executables and archives belong under ignored `vendor/local/`, not in this directory.

`build.ps1` configures and builds the Arm firmware and/or the native unit tests. It does not flash hardware or execute anything from `vendor/local/`.

```powershell
pwsh -File tools/build.ps1 -Target all
```
