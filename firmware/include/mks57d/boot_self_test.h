#ifndef MKS57D_BOOT_SELF_TEST_H
#define MKS57D_BOOT_SELF_TEST_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    BOOT_SELF_TEST_EARLY_MEMORY = 1u << 0,
    BOOT_SELF_TEST_CLOCK = 1u << 1,
    BOOT_SELF_TEST_INTERRUPT_POLICY = 1u << 2,
    BOOT_SELF_TEST_PASSIVE_BOARD = 1u << 3,
    BOOT_SELF_TEST_TIMEBASE = 1u << 4,
    BOOT_SELF_TEST_APPLICATION_STATE = 1u << 5,
    BOOT_SELF_TEST_WATCHDOG = 1u << 6,
    BOOT_SELF_TEST_REQUIRED_PASSIVE =
        BOOT_SELF_TEST_EARLY_MEMORY |
        BOOT_SELF_TEST_CLOCK |
        BOOT_SELF_TEST_INTERRUPT_POLICY |
        BOOT_SELF_TEST_PASSIVE_BOARD |
        BOOT_SELF_TEST_TIMEBASE |
        BOOT_SELF_TEST_APPLICATION_STATE |
        BOOT_SELF_TEST_WATCHDOG
};

typedef struct
{
    uint32_t required;
    uint32_t passed;
    uint32_t failed;
} boot_self_test_t;

void boot_self_test_init(boot_self_test_t *self_test, uint32_t required);
void boot_self_test_pass(boot_self_test_t *self_test, uint32_t checks);
void boot_self_test_fail(boot_self_test_t *self_test, uint32_t checks);
bool boot_self_test_ready(const boot_self_test_t *self_test);

#endif
