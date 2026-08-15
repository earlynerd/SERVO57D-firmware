# External Reference Inventory

External artifacts are stored locally for research but ignored by Git. Before public release, add canonical source URLs, retrieval dates, and redistribution conclusions.

## Nations Technologies packages

Local directory: `vendor/local/archives/nationstech/`

| Package | Size | SHA-256 | Use |
| --- | ---: | --- | --- |
| `AN_N32G43x_N32L43x_N32L40x_GCC_Development_Environment_Application_Note_V3.1.zip` | 3,150,739 | `ABEB5ACEB50BC5E88F4AB80DE3933B3BA49A2201D26426E948ED0BD45638756D` | GCC setup note and example material |
| `JLink_tool_adds_Nations_chip_V1.6.0.zip` | 3,614,805 | `C808511BAA27D3BC6B3A93DE8F34DC5F08789FE7E3547ADD366FA89690DE89C0` | N32 J-Link device definitions, loaders, J-Flash projects, unlock tool |
| `N32L40x_FLASH_Programming_Algorithm.1.1.0.zip` | 12,455 | `D507A660802C3A5AA15D2BEAD2D6D7B190068CD30D8816C1135CA8C79F4ACC16` | Source for the N32L40x CMSIS flash algorithm |
| `Nations.N32L40x_Library.2.3.0.zip` | 5,654,526 | `1D43FB9C88458A08C666C4D768C2B04D4600743E420183649BF30ACCAADE517B` | CMSIS, startup, linker, drivers, middleware, and examples |

The Nations driver and flash-algorithm source inspected so far uses permissive redistribution terms requiring preservation of the copyright notice, conditions, and disclaimer. Binary libraries and bundled third-party middleware require separate review.

## CMSIS device pack

Local file: `reference/local/NSING.N32L40x_DFP.1.0.2.pack`

- Size: 715,205 bytes
- SHA-256: `25AAD277443CE25CB908852F40DDBDF4BB70CA7EF320A40E7B5E0B205EA15259`
- Intended use: device description, SVD, startup support, and pyOCD flash/debug target data.

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

## Publication checklist

- [ ] Record the official download URL for each Nations package.
- [ ] Record the Makerbase repository/release URL for schematics and manuals.
- [ ] Confirm which external documents may be redistributed versus linked.
- [ ] Decide whether to vendor the minimal Nations driver subset or fetch it during setup.
- [ ] Add a third-party notices file when project-owned firmware begins importing source.

