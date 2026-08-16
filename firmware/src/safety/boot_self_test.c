#include "mks57d/boot_self_test.h"

#include <stddef.h>

void boot_self_test_init(boot_self_test_t *self_test, uint32_t required)
{
    if (self_test == NULL)
    {
        return;
    }

    self_test->required = required;
    self_test->passed = 0u;
    self_test->failed = 0u;
}

void boot_self_test_pass(boot_self_test_t *self_test, uint32_t checks)
{
    if (self_test == NULL)
    {
        return;
    }

    self_test->passed |= checks & ~self_test->failed;
}

void boot_self_test_fail(boot_self_test_t *self_test, uint32_t checks)
{
    if (self_test == NULL)
    {
        return;
    }

    self_test->failed |= checks;
    self_test->passed &= ~checks;
}

bool boot_self_test_ready(const boot_self_test_t *self_test)
{
    if (self_test == NULL)
    {
        return false;
    }

    return (self_test->failed == 0u) &&
           ((self_test->passed & self_test->required) == self_test->required);
}
