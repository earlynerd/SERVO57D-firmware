#include <stdbool.h>
#include <stdio.h>

#include "mks57d/app_state.h"
#include "mks57d/fault_latch.h"

static unsigned int s_failures;

#define EXPECT_TRUE(expression)                                                     \
    do                                                                              \
    {                                                                               \
        if (!(expression))                                                          \
        {                                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);          \
            ++s_failures;                                                           \
        }                                                                           \
    } while (false)

static void test_reset_only_enters_diagnostic_after_passive_init(void)
{
    const app_transition_context_t unsafe = {.safe_to_recover = false};

    EXPECT_TRUE(app_state_transition(APP_STATE_RESET_SAFE,
                                     APP_EVENT_FAULT_ACKNOWLEDGED,
                                     unsafe) == APP_STATE_RESET_SAFE);
    EXPECT_TRUE(app_state_transition(APP_STATE_RESET_SAFE,
                                     APP_EVENT_PASSIVE_INIT_COMPLETE,
                                     unsafe) == APP_STATE_DIAGNOSTIC);
}

static void test_faults_converge_on_fault_state(void)
{
    const app_transition_context_t unsafe = {.safe_to_recover = false};

    EXPECT_TRUE(app_state_transition(APP_STATE_RESET_SAFE,
                                     APP_EVENT_FAULT_DETECTED,
                                     unsafe) == APP_STATE_FAULT);
    EXPECT_TRUE(app_state_transition(APP_STATE_RUN,
                                     APP_EVENT_FAULT_DETECTED,
                                     unsafe) == APP_STATE_FAULT);
}

static void test_fault_recovery_requires_explicit_safe_context(void)
{
    const app_transition_context_t unsafe = {.safe_to_recover = false};
    const app_transition_context_t safe = {.safe_to_recover = true};

    EXPECT_TRUE(app_state_transition(APP_STATE_FAULT,
                                     APP_EVENT_FAULT_ACKNOWLEDGED,
                                     unsafe) == APP_STATE_FAULT);
    EXPECT_TRUE(app_state_transition(APP_STATE_FAULT,
                                     APP_EVENT_FAULT_ACKNOWLEDGED,
                                     safe) == APP_STATE_DIAGNOSTIC);
}

static void test_fault_latch_preserves_first_fault_and_accumulates_flags(void)
{
    fault_latch_t latch;

    fault_latch_init(&latch);
    EXPECT_TRUE(!fault_latch_is_active(&latch));
    EXPECT_TRUE(latch.first == FAULT_SOURCE_NONE);

    fault_latch_raise(&latch, FAULT_SOURCE_CLOCK);
    fault_latch_raise(&latch, FAULT_SOURCE_CORE_EXCEPTION);

    EXPECT_TRUE(fault_latch_is_active(&latch));
    EXPECT_TRUE(latch.first == FAULT_SOURCE_CLOCK);
    EXPECT_TRUE((latch.active & FAULT_SOURCE_CLOCK) != 0u);
    EXPECT_TRUE((latch.active & FAULT_SOURCE_CORE_EXCEPTION) != 0u);
}

int main(void)
{
    test_reset_only_enters_diagnostic_after_passive_init();
    test_faults_converge_on_fault_state();
    test_fault_recovery_requires_explicit_safe_context();
    test_fault_latch_preserves_first_fault_and_accumulates_flags();

    if (s_failures != 0u)
    {
        printf("%u test assertion(s) failed\n", s_failures);
        return 1;
    }

    puts("all host unit tests passed");
    return 0;
}
