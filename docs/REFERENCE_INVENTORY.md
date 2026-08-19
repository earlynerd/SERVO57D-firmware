# External Reference Inventory

External artifacts are stored locally for research but ignored by Git. Before public release, add canonical source URLs, retrieval dates, and redistribution conclusions.

## Nations Technologies packages

Local directory: `vendor/local/archives/nationstech/`

Canonical product and download page: <https://www.nationstech.com/product/general/n32l/n32l40x/>

Retrieved/verified: 2026-08-15. The official page identifies the N32L40x SDK as version V2.3.0 and the GCC setup package as version V3.1.

| Package | Size | SHA-256 | Use |
| --- | ---: | --- | --- |
| `AN_N32G43x_N32L43x_N32L40x_GCC_Development_Environment_Application_Note_V3.1.zip` | 3,150,739 | `ABEB5ACEB50BC5E88F4AB80DE3933B3BA49A2201D26426E948ED0BD45638756D` | GCC setup note and example material |
| `JLink_tool_adds_Nations_chip_V1.6.0.zip` | 3,614,805 | `C808511BAA27D3BC6B3A93DE8F34DC5F08789FE7E3547ADD366FA89690DE89C0` | N32 J-Link device definitions, loaders, J-Flash projects, unlock tool |
| `N32L40x_FLASH_Programming_Algorithm.1.1.0.zip` | 12,455 | `D507A660802C3A5AA15D2BEAD2D6D7B190068CD30D8816C1135CA8C79F4ACC16` | Source for the N32L40x CMSIS flash algorithm |
| `Nations.N32L40x_Library.2.3.0.zip` | 5,654,526 | `1D43FB9C88458A08C666C4D768C2B04D4600743E420183649BF30ACCAADE517B` | CMSIS, startup, linker, drivers, middleware, and examples |

The Nations driver and flash-algorithm source inspected so far uses permissive redistribution terms requiring preservation of the copyright notice, conditions, and disclaimer. Binary libraries and bundled third-party middleware require separate review.

The passive ADC scaffold was checked against the N32L40x User Manual V2.6.0
and the SDK's V1.2.2 ADC driver. No ADC driver source was imported. The
project-owned register layer records one SDK-only requirement: value `0x28` is
set in the otherwise unnamed ADC analog-LDO control register at ADC offset
`0x60`. That detail remains hardware-gated because the user manual does not
document the register.

## CMSIS device pack

Local file: `reference/local/NSING.N32L40x_DFP.1.0.2.pack`

- Size: 715,205 bytes
- SHA-256: `25AAD277443CE25CB908852F40DDBDF4BB70CA7EF320A40E7B5E0B205EA15259`
- Intended use: device description, SVD, startup support, and pyOCD flash/debug target data.

### Known metadata conflicts

N32L40x User Manual V2.6 is the current authority used by the linker and startup code. Its N32L406xB memory table defines 16 KiB SRAM1 at `0x20000000`, an 8 KiB gap, and 8 KiB SRAM2 at `0x20006000`. The SDK's generic linker instead exposes 32 KiB contiguously, while CMSIS-Pack device metadata describes 24 KiB contiguously. Neither external layout is used by the project.

The SDK's generic `system_n32l40x.c` also exposes configurations above the N32L406's documented 64 MHz maximum and performs a voltage-mode sequence that does not clearly match the current user manual. It is retained as reference material but is not compiled into the firmware.

## Nations download utility

Local directory: `vendor/local/tools/NationsMCUDownloadTool/`

- Main executable: `NZDownloadTool.exe`
- Size: 2,460,672 bytes
- SHA-256: `1F438A3A78010564A18149B40BA2C068688FD85BBC36DD6717886B2C073C48BB`
- Inspected statically; not executed during repository preparation.
- Contains an exact N32L406CB device definition and a protection-aware N32L40x flash algorithm.

## Makerbase and component references

Local directory: `reference/local/`

Currently includes:

- Published MKS SERVO28D/35D/42D/57D schematics.
- SERVO42D/57D RS-485, CAN, and Modbus manuals.
- Makerbase Arduino host examples and configuration utility material.
- N32L40x Series User Manual V2.6.0.
- Component datasheet material and extracted schematic images.

The RS-485 direction truth table and enable/disable timing use MaxLinear's
official `SP481E / SP485E` Rev. 1.0.6 datasheet (2022-03-17):
<https://www.maxlinear.com/ds/sp481e_sp485e.pdf>. No copy is added to the
repository.

## Independent open firmware references

### Delsian `servo57d`

- Public source: <https://github.com/Delsian/servo57d>
- Inspected revision: `5d400b01bc8afee0f2c17f4039408f82b5164584`
- Repository license: GNU GPL v3
- Use in this project: secondary factual evidence for the N32 I2C and ADC pin
  mappings, 100 kHz I2C bus, provisional current/bus ADC sampling-time split,
  and exact-board MT6816 SPI pin/alternate-function and four-byte burst shape.
  Its local `ZJY042-7240TSWEG01` module datasheet supplies the candidate SSD1306
  identity, active-low reset, 72-by-40 geometry, `0x3C` 7-bit address, and
  initialization values. Its ignored `magntek_mt6816ctstdr_apr22_xonlink.pdf`
  is the MagnTek MT6816 Rev. 1.9 (2021-12) datasheet used for mode-3 timing,
  parity, register, warning, and power-up behavior. No source, fonts, binary
  assets, or external PDFs are imported.

The project-owned I2C transport, display protocol layer, passive ADC layer, SPI
transport, and MT6816 decoder are clean-sheet implementations. The fitted
display profile, passive ADC acquisition, and encoder motion/wrap behavior are
bench-proven; exact part identity, dynamic ADC behavior, and control-rate
encoder signal integrity remain open.

## Publication checklist

- [x] Record the official product/download page and published versions for the Nations SDK and GCC package.
- [ ] Record direct, version-stable download URLs for each Nations archive if the site exposes them.
- [ ] Record the Makerbase repository/release URL for schematics and manuals.
- [ ] Confirm which external documents may be redistributed versus linked.
- [ ] Decide whether to vendor the minimal Nations driver subset or fetch it during setup.
- [ ] Add a third-party notices file when project-owned firmware begins importing source.
