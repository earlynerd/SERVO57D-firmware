# Clock Bring-up

## First-image policy

The first hardware image verifies and retains the reset-default 4 MHz MSI clock. It uses bounded waits and readback checks, configures AHB/APB prescalers to divide by one, and leaves the voltage range, cache controls, HSI, HSE, and PLL untouched.

This conservative policy avoids relying on the SDK's generic system-clock source. That source contains voltage-mode operations that do not clearly match N32L40x User Manual V2.6 and permits frequencies above the N32L406's documented 64 MHz maximum. The vendor RCC example also applies a silicon-identifier workaround when selecting HSI or HSE directly, so even an apparently simple move to 16 MHz HSI deserves hardware validation.

If reset state, readiness, source selection, or calculated frequency is unexpected, startup records diagnostic registers and enters the common panic path. `SystemCoreClockUpdate` intentionally treats PLL as unsupported until the 64 MHz path below is implemented and tested.

## Deferred 64 MHz plan

Both available SERVO57D schematics show an 8 MHz crystal. The preferred eventual configuration is therefore HSE 8 MHz multiplied by 8. HSI divided by 2 and multiplied by 8 is a possible fallback, but its accuracy and the documented silicon workaround must be considered.

The implementation should proceed as a separately reviewed change:

1. Begin on MSI and preserve it as the fallback clock.
2. Enable the chosen source and wait with a bounded timeout.
3. Before increasing HCLK, set Flash latency to one wait state, AHB to divide by one, APB2 to divide by two, and APB1 to divide by four.
4. With PLL disabled, select HSE or HSI/2 and the ×8 multiplier.
5. Enable PLL and wait with a bounded timeout.
6. Switch the system clock to PLL and verify the selected-source status with a bounded timeout.
7. Calculate and verify 64 MHz from register readback; on any failure, remain on or return to MSI and panic safely.
8. Validate SysTick timing, peripheral clocks, and the actual frequency on hardware before making 64 MHz the default.

Do not copy the SDK's 108 MHz options; they exceed the N32L406 rating. Do not route MCO to PA8 for clock measurement on this board: PA8 is provisionally assigned to the isolated `nDIR` input. Use timing measurements or a board-safe observation point instead.

The required main-regulator voltage-range sequence remains unresolved because the vendor implementation and current user manual do not agree clearly enough. That question must be settled against the exact silicon revision and measured behavior before the PLL path is enabled.
