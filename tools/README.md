# Project Tools

Project-owned host tools, probe firmware configuration, build helpers, and test utilities live here.

Manufacturer executables and archives belong under ignored `vendor/local/`, not in this directory.

`build.ps1` configures and builds the Arm firmware and/or the native unit tests. It does not flash hardware or execute anything from `vendor/local/`.

```powershell
pwsh -File tools/build.ps1 -Target all
```

`mks57d_rs485.py` is the native-v1 commissioning console. It uses PySerial,
prints machine-readable JSON, and can inspect the current loop before, during,
and after a duration-bounded remote run:

```powershell
py tools/mks57d_rs485.py list
py tools/mks57d_rs485.py --port COM29 identity
py tools/mks57d_rs485.py --port COM29 boot
py tools/mks57d_rs485.py --port COM29 encoder
py tools/mks57d_rs485.py --port COM29 status
py tools/mks57d_rs485.py --port COM29 configure --counts 25 --frequency-hz 0.5
py tools/mks57d_rs485.py --port COM29 run --leg A1 --duration-ms 10000
py tools/mks57d_rs485.py --port COM29 stop
```

`encoder` reports the live 14-bit magnetic angle and health counters. `watch`
and `run` attach the same encoder snapshot to every current-loop record so
electrical commutation and physical rotor motion can be compared directly.

`flash-jlink.ps1` builds and validates the bridge-safe diagnostic image, then
programs and verifies it with SEGGER J-Link Commander using the exact
`N32L406CB` target. Without `-Yes`, it performs a dry run and does not access
the probe or target:

```powershell
pwsh -File tools/flash-jlink.ps1
pwsh -File tools/flash-jlink.ps1 -Yes
```

The first flash must use a current-limited supply with the motor disconnected.
The script requires RDP L0 before programming and does not unlock read
protection or modify option bytes.

`reference_cache.py` verifies cataloged local PDFs, extracts searchable
page-level text, and creates provenance-tracked page renders on demand. Its
generated cache is ignored. See [the reference-cache workflow](../docs/REFERENCE_CACHE.md).

```powershell
python tools/reference_cache.py status
python tools/reference_cache.py build n32l40x-um-v2.6
python tools/reference_cache.py search "SRAM2 parity"
```
