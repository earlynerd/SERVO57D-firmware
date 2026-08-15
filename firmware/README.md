# Firmware

Project-owned embedded firmware will live here after the debug-access feasibility gate succeeds.

The first target is a minimal N32L406CBL7 image that establishes safe GPIO defaults, remains recoverable over SWD, and blinks the onboard LED. Motor-control code must not be added before the passive bring-up and power-stage timing gates in `PLAN.md`.

The intended initial implementation is a small GCC/CMSIS bare-metal build using only the required Nations peripheral sources. Build-system and directory choices remain open until the first board can be programmed.

