# Clock Configuration

## Production clock tree

Firmware 0.17.8 retains the promotion from the reset-default 4 MHz MSI to the
N32L406's documented 64 MHz maximum. Both published SERVO57D schematics and
the physical board identify an 8 MHz crystal, so the selected path is HSE
undivided into PLL times eight. This configuration is bench-proven.

| Domain | Frequency | Derivation |
| --- | ---: | --- |
| SYSCLK / HCLK / Cortex-M4 | 64 MHz | 8 MHz HSE x8, AHB /1 |
| PCLK2 | 32 MHz | HCLK /2 |
| SPI1, USART1 | 32 MHz | APB2 peripherals |
| PCLK1 | 16 MHz | HCLK /4 |
| I2C1 | 16 MHz | APB1 peripheral |
| TIM3 | 32 MHz | PCLK1 x2 because the APB1 prescaler is not /1 |
| ADC synchronous sample clock | 2 MHz | HCLK /32 |
| ADC timing clock | 1 MHz | 16 MHz HSI /16 |

TIM3 uses 1600 edge-aligned counts per 20 kHz period. SPI1 remains at 500 kHz,
USART1 remains at 115200 baud, and I2C1 deliberately selects the same
approximately 333.3 kHz rate already proven with the OLED.

## Startup and failure contract

`platform_clock_init()` begins while MSI is still the active source and:

1. verifies reset MSI state and readiness;
2. configures AHB /1, APB2 /2, and APB1 /4 before increasing SYSCLK;
3. configures one Flash wait state, as required for 32-64 MHz HCLK;
4. enables HSE and waits with a bounded timeout;
5. while PLL is disabled, selects HSE undivided and the x8 multiplier;
6. enables PLL and waits with a bounded timeout;
7. switches SYSCLK to PLL and waits for hardware source-status confirmation;
8. decodes `SystemCoreClock` from register state and verifies every source,
   prescaler, Flash-latency, APB-clock, and timer-clock result.

Any failure occurs before bridge initialization, records the observed clock
registers, and enters the common panic path. The code does not copy the SDK's
options above 64 MHz. It leaves the reset main-regulator state unchanged; the
manual's MR 1.0 V sequence is a low-power transition and is not required for
the 64 MHz run configuration.

Do not route MCO to PA8 for measurement because PA8 is the bench-proven
isolated `nDIR` input. Clock acceptance instead uses the mandatory clock-source
readback plus normal heartbeat, OLED, encoder, ADC, and communications behavior.
