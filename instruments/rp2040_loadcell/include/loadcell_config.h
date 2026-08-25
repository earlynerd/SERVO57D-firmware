#pragma once

#include <stddef.h>
#include <stdint.h>

// Pin values are intentionally supplied by the PlatformIO environment. A
// negative I2C pair produces an inert, protocol-queryable build. A negative
// DRDY selects cycle-ready polling over I2C instead of guessing a GPIO.
#ifndef LOADCELL_SDA_PIN
#define LOADCELL_SDA_PIN (-1)
#endif

#ifndef LOADCELL_SCL_PIN
#define LOADCELL_SCL_PIN (-1)
#endif

#ifndef LOADCELL_DRDY_PIN
#define LOADCELL_DRDY_PIN (-1)
#endif

#ifndef LOADCELL_LED_BLUE_PIN
#define LOADCELL_LED_BLUE_PIN (-1)
#endif

#ifndef LOADCELL_LED_RED_PIN
#define LOADCELL_LED_RED_PIN (-1)
#endif

#ifndef LOADCELL_LED_GREEN_PIN
#define LOADCELL_LED_GREEN_PIN (-1)
#endif

#ifndef LOADCELL_LED_ACTIVE_HIGH
#define LOADCELL_LED_ACTIVE_HIGH 1
#endif

#ifndef LOADCELL_I2C_HZ
#define LOADCELL_I2C_HZ 400000U
#endif

#ifndef LOADCELL_I2C_INSTANCE
#define LOADCELL_I2C_INSTANCE 0
#endif

#ifndef LOADCELL_DEFAULT_SAMPLE_RATE_SPS
#define LOADCELL_DEFAULT_SAMPLE_RATE_SPS 320U
#endif

#ifndef LOADCELL_DEFAULT_GAIN
#define LOADCELL_DEFAULT_GAIN 128U
#endif

#if (LOADCELL_SDA_PIN < 0) && (LOADCELL_SCL_PIN < 0)
#define LOADCELL_I2C_CONFIGURED 0
#elif (LOADCELL_SDA_PIN < 0) || (LOADCELL_SCL_PIN < 0)
#error "LOADCELL_SDA_PIN and LOADCELL_SCL_PIN must be assigned together"
#else
#define LOADCELL_I2C_CONFIGURED 1
#endif

#if LOADCELL_DRDY_PIN < 0
#define LOADCELL_DRDY_CONFIGURED 0
#else
#define LOADCELL_DRDY_CONFIGURED 1
#endif

#if LOADCELL_LED_BLUE_PIN < 0
#define LOADCELL_LED_BLUE_CONFIGURED 0
#else
#define LOADCELL_LED_BLUE_CONFIGURED 1
#endif

#if LOADCELL_LED_RED_PIN < 0
#define LOADCELL_LED_RED_CONFIGURED 0
#else
#define LOADCELL_LED_RED_CONFIGURED 1
#endif

#if LOADCELL_LED_GREEN_PIN < 0
#define LOADCELL_LED_GREEN_CONFIGURED 0
#else
#define LOADCELL_LED_GREEN_CONFIGURED 1
#endif

#define LOADCELL_LEDS_CONFIGURED                                      \
  (LOADCELL_LED_BLUE_CONFIGURED || LOADCELL_LED_RED_CONFIGURED ||    \
   LOADCELL_LED_GREEN_CONFIGURED)

// Retained as the hardware-availability guard used by the implementation.
// DRDY is optional because cycle-ready can be polled through PU_CTRL.CR.
#define LOADCELL_PINS_CONFIGURED LOADCELL_I2C_CONFIGURED

#if (LOADCELL_I2C_INSTANCE != 0) && (LOADCELL_I2C_INSTANCE != 1)
#error "LOADCELL_I2C_INSTANCE must select RP2040 I2C controller 0 or 1"
#endif

#if LOADCELL_I2C_CONFIGURED
#if (LOADCELL_SDA_PIN > 29) || (LOADCELL_SCL_PIN > 29)
#error "RP2040 load-cell I2C pins must be GPIO 0 through GPIO 29"
#endif
#if LOADCELL_SDA_PIN == LOADCELL_SCL_PIN
#error "RP2040 load-cell SDA and SCL pins must be distinct"
#endif
#if LOADCELL_I2C_INSTANCE == 0
#if ((LOADCELL_SDA_PIN % 4) != 0) || ((LOADCELL_SCL_PIN % 4) != 1)
#error "RP2040 I2C0 requires SDA GPIO modulo 4 == 0 and SCL modulo 4 == 1"
#endif
#else
#if ((LOADCELL_SDA_PIN % 4) != 2) || ((LOADCELL_SCL_PIN % 4) != 3)
#error "RP2040 I2C1 requires SDA GPIO modulo 4 == 2 and SCL modulo 4 == 3"
#endif
#endif

#endif

#if LOADCELL_DRDY_CONFIGURED
#if !LOADCELL_I2C_CONFIGURED
#error "LOADCELL_DRDY_PIN cannot be used without an I2C pin pair"
#endif
#if LOADCELL_DRDY_PIN > 29
#error "RP2040 load-cell DRDY must be GPIO 0 through GPIO 29"
#endif
#if (LOADCELL_DRDY_PIN == LOADCELL_SDA_PIN) || \
    (LOADCELL_DRDY_PIN == LOADCELL_SCL_PIN)
#error "RP2040 load-cell DRDY must be distinct from SDA and SCL"
#endif
#endif

#if (LOADCELL_LED_ACTIVE_HIGH != 0) && (LOADCELL_LED_ACTIVE_HIGH != 1)
#error "LOADCELL_LED_ACTIVE_HIGH must be 0 or 1"
#endif

#if (LOADCELL_LED_BLUE_PIN < -1) || (LOADCELL_LED_BLUE_PIN > 29) || \
    (LOADCELL_LED_RED_PIN < -1) || (LOADCELL_LED_RED_PIN > 29) ||   \
    (LOADCELL_LED_GREEN_PIN < -1) || (LOADCELL_LED_GREEN_PIN > 29)
#error "RP2040 indicator LED pins must be -1 or GPIO 0 through GPIO 29"
#endif

#if LOADCELL_LED_BLUE_CONFIGURED &&                                  \
    ((LOADCELL_LED_BLUE_PIN == LOADCELL_SDA_PIN) ||                  \
     (LOADCELL_LED_BLUE_PIN == LOADCELL_SCL_PIN) ||                  \
     (LOADCELL_DRDY_CONFIGURED &&                                    \
      (LOADCELL_LED_BLUE_PIN == LOADCELL_DRDY_PIN)))
#error "Blue indicator LED pin conflicts with a load-cell interface pin"
#endif

#if LOADCELL_LED_RED_CONFIGURED &&                                   \
    ((LOADCELL_LED_RED_PIN == LOADCELL_SDA_PIN) ||                   \
     (LOADCELL_LED_RED_PIN == LOADCELL_SCL_PIN) ||                   \
     (LOADCELL_DRDY_CONFIGURED &&                                    \
      (LOADCELL_LED_RED_PIN == LOADCELL_DRDY_PIN)))
#error "Red indicator LED pin conflicts with a load-cell interface pin"
#endif

#if LOADCELL_LED_GREEN_CONFIGURED &&                                 \
    ((LOADCELL_LED_GREEN_PIN == LOADCELL_SDA_PIN) ||                 \
     (LOADCELL_LED_GREEN_PIN == LOADCELL_SCL_PIN) ||                 \
     (LOADCELL_DRDY_CONFIGURED &&                                    \
      (LOADCELL_LED_GREEN_PIN == LOADCELL_DRDY_PIN)))
#error "Green indicator LED pin conflicts with a load-cell interface pin"
#endif

#if LOADCELL_LED_BLUE_CONFIGURED && LOADCELL_LED_RED_CONFIGURED && \
    (LOADCELL_LED_BLUE_PIN == LOADCELL_LED_RED_PIN)
#error "Blue and red indicator LEDs must use distinct pins"
#endif

#if LOADCELL_LED_BLUE_CONFIGURED && LOADCELL_LED_GREEN_CONFIGURED && \
    (LOADCELL_LED_BLUE_PIN == LOADCELL_LED_GREEN_PIN)
#error "Blue and green indicator LEDs must use distinct pins"
#endif

#if LOADCELL_LED_RED_CONFIGURED && LOADCELL_LED_GREEN_CONFIGURED && \
    (LOADCELL_LED_RED_PIN == LOADCELL_LED_GREEN_PIN)
#error "Red and green indicator LEDs must use distinct pins"
#endif

#if (LOADCELL_DEFAULT_SAMPLE_RATE_SPS != 10) && \
    (LOADCELL_DEFAULT_SAMPLE_RATE_SPS != 20) && \
    (LOADCELL_DEFAULT_SAMPLE_RATE_SPS != 40) && \
    (LOADCELL_DEFAULT_SAMPLE_RATE_SPS != 80) && \
    (LOADCELL_DEFAULT_SAMPLE_RATE_SPS != 320)
#error "LOADCELL_DEFAULT_SAMPLE_RATE_SPS must be 10, 20, 40, 80, or 320"
#endif

#if (LOADCELL_DEFAULT_GAIN != 1) && (LOADCELL_DEFAULT_GAIN != 2) && \
    (LOADCELL_DEFAULT_GAIN != 4) && (LOADCELL_DEFAULT_GAIN != 8) && \
    (LOADCELL_DEFAULT_GAIN != 16) && (LOADCELL_DEFAULT_GAIN != 32) && \
    (LOADCELL_DEFAULT_GAIN != 64) && (LOADCELL_DEFAULT_GAIN != 128)
#error "LOADCELL_DEFAULT_GAIN must be 1, 2, 4, 8, 16, 32, 64, or 128"
#endif

namespace loadcell {

constexpr uint8_t kNau7802Address = 0x2A;
constexpr uint8_t kNau7802PuCtrlRegister = 0x00;
constexpr uint8_t kNau7802CycleReadyMask = 1U << 5;
constexpr uint8_t kNau7802AdcoB2Register = 0x12;

constexpr size_t kDrdyQueueCapacity = 32;
constexpr size_t kOutputQueueCapacity = 256;
constexpr size_t kResponseQueueCapacity = 8;
constexpr size_t kCommandLineCapacity = 128;
constexpr size_t kProtocolLineCapacity = 384;
constexpr size_t kMaxTxChunk = 64;
constexpr uint32_t kMaxTareSamples = 1024;
constexpr uint64_t kTareTimeoutCeilingUs = 300000000ULL;
constexpr uint8_t kI2cReadAttempts = 3;
constexpr uint64_t kReadyPollIntervalUs = 250ULL;

static_assert((kDrdyQueueCapacity & (kDrdyQueueCapacity - 1U)) == 0U,
              "DRDY queue capacity must be a power of two");
static_assert((kOutputQueueCapacity & (kOutputQueueCapacity - 1U)) == 0U,
              "output queue capacity must be a power of two");

}  // namespace loadcell
