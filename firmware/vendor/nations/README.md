# Nations N32L40x support subset

This directory contains the minimal CMSIS/device subset imported from `Nations.N32L40x_Library.2.3.0`:

- CMSIS Cortex-M4 core headers required by the device header.
- `n32l40x.h`, `system_n32l40x.h`, and the power header retained for device definitions and reference.
- `system_n32l40x.c`, retained for provenance and comparison but not compiled.
- The GCC startup/vector-table source, modified only so its weak default handler branches to the project panic path.

No middleware, example code, binary library, or full standard peripheral library is vendored.

Canonical source page: <https://www.nationstech.com/product/general/n32l/n32l40x/>

Source archive SHA-256: `1D43FB9C88458A08C666C4D768C2B04D4600743E420183649BF30ACCAADE517B`

The imported files retain their Nations or Arm copyright and license headers. Arm CMSIS core headers are Apache-2.0 licensed; see `../../../LICENSES/Apache-2.0.txt`. Project-owned `src/platform/system.c` replaces the vendor clock source in the build. The project linker follows the newer user manual's discontiguous SRAM map rather than the SDK's generic contiguous 32 KiB region.
