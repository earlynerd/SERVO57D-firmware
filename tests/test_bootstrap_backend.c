#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mks57d/adc1.h"
#include "mks57d/board.h"
#include "mks57d/current_loop_backend.h"
#include "mks57d/cycle_counter.h"
#include "mks57d/platform.h"
#include "mks57d/tim3_bridge_pwm.h"
#include "mks57d/timebase.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); } } while (0)

uint32_t mock_basepri;
static adc1_current_event_handler_t adc_handler;
static void* adc_context;
static tim3_bridge_pwm_update_handler_t update_handler;
static void* update_context;
static bool timer_ready = true;
static bool update_irq_enabled;
static bool probe_stage_ok = true;
static unsigned probe_stages;
static unsigned direct_zero_count;
static uint16_t duties[4];
static adc1_status_t read_status = ADC1_STATUS_OK;
static adc1_current_snapshot_t sample = {
    .current_a_raw = 2040u, .current_b_raw = 2050u,
};
static uint32_t mock_micros = 10000u;

bool adc1_set_current_event_handler(adc1_current_event_handler_t handler,
                                    void* context)
{
    adc_handler = handler;
    adc_context = context;
    return true;
}
bool adc1_set_current_timing_capture(bool enabled)
{
    (void)enabled;
    return true;
}
adc1_status_t adc1_restart_pwm_synchronized_current(void)
{
    return ADC1_STATUS_OK;
}
adc1_status_t adc1_read_synchronized_current(adc1_current_snapshot_t* output)
{
    *output = sample;
    return read_status;
}
bool tim3_bridge_pwm_stage_duties(const uint16_t requested[4])
{
    if (!timer_ready) { return false; }
    memcpy(duties, requested, sizeof(duties));
    return true;
}
bool tim3_bridge_pwm_stage_bootstrap_probe(void)
{
    if (!timer_ready || !probe_stage_ok) { return false; }
    for (unsigned i = 0; i < 4u; ++i) { duties[i] = 1000u; }
    ++probe_stages;
    return true;
}
bool tim3_bridge_pwm_get_preload_margin_ticks(uint16_t* ticks)
{
    *ticks = 1500u;
    return timer_ready;
}
bool tim3_bridge_pwm_zero(void)
{
    if (!timer_ready) { return false; }
    memset(duties, 0, sizeof(duties));
    return true;
}
bool tim3_bridge_pwm_set_update_handler(
    tim3_bridge_pwm_update_handler_t handler, void* context)
{
    update_handler = handler;
    update_context = context;
    return timer_ready;
}
bool tim3_bridge_pwm_update_irq_enable(bool enable)
{
    update_irq_enabled = enable;
    return timer_ready;
}
void board_bridge_force_low_zero(void)
{
    ++direct_zero_count;
    memset(duties, 0, sizeof(duties));
    timer_ready = false;
    update_irq_enabled = false;
}
bool board_bridge_pwm_init(uint32_t clock_hz)
{
    timer_ready = clock_hz != 0u;
    memset(duties, 0, sizeof(duties));
    return timer_ready;
}
uint32_t platform_apb1_timer_clock_hz(void) { return 64000000u; }
uint32_t timebase_micros(void) { return mock_micros; }
uint32_t timebase_millis(void) { return mock_micros / 1000u; }
bool cycle_counter_init(void) { return true; }
uint32_t cycle_counter_read(void) { return mock_micros * 64u; }

static current_loop_backend_snapshot_t state(void)
{
    current_loop_backend_snapshot_t result = {0};
    current_loop_backend_get_snapshot(&result);
    CHECK(mock_basepri == 0u);
    return result;
}
static void check_zero(void)
{
    for (unsigned i = 0; i < 4u; ++i) { CHECK(duties[i] == 0u); }
}
static void deliver_sample(void)
{
    CHECK(adc_handler != NULL);
    adc_handler(ADC1_STATUS_OK, &sample, adc_context);
}
static void timer_tick(bool fresh_sample)
{
    if (fresh_sample) { deliver_sample(); }
    if (update_irq_enabled)
    {
        CHECK(update_handler != NULL);
        update_handler(update_context);
    }
    /* Deliberately freeze wall time: duration and VBUS lease must be bounded
     * by carrier interrupts even if low-priority timekeeping stops running. */
}
static void initialize(void)
{
    const phase_current_loop_config_t config = {
        .current_a_zero_raw = 2040u, .current_b_zero_raw = 2050u,
        .reference_limit_counts = 495u, .hard_current_limit_counts = 600u,
        .proportional_gain_q16_per_count = 2 * PHASE_CURRENT_LOOP_Q16_ONE,
        .integral_gain_q16_per_count_per_step = PHASE_CURRENT_LOOP_Q16_ONE / 16,
        .phase_voltage_limit_permille = 700u, .duty_margin_permille = 200u,
        .current_a_polarity = 1, .current_b_polarity = -1,
    };
    const electrical_phase_predictor_config_t predictor = {
        .electrical_cycles_per_mechanical_revolution = 50u,
        .output_lead_us = 55u, .maximum_prediction_age_us = 2000u,
        .maximum_mechanical_velocity_q16_16 = 20 * 65536,
    };
    CHECK(current_loop_backend_init(&config, &predictor));
    deliver_sample();
    CHECK(state().initialized);
}
static void begin_probe(uint32_t millis)
{
    CHECK(current_loop_backend_start_bootstrap_probe(millis, 895u));
    CHECK(state().active);
    CHECK(state().bootstrap_probe_active);
    CHECK(!state().bootstrap_probe_completed);
    deliver_sample();
    CHECK(probe_stages == 1u);
    for (unsigned i = 0; i < 4u; ++i) { CHECK(duties[i] == 1000u); }
}
static void expect_fault(uint32_t flag)
{
    const current_loop_backend_snapshot_t snapshot = state();
    CHECK(!snapshot.active);
    CHECK(!snapshot.bootstrap_probe_active);
    CHECK(!snapshot.bootstrap_probe_completed);
    CHECK((snapshot.fault_flags & flag) != 0u);
    CHECK(direct_zero_count > 0u);
    check_zero();
    CHECK(!current_loop_backend_start_bootstrap_probe(1u, 895u));
    CHECK(current_loop_backend_stop());
    check_zero();
}
static void boundaries(void)
{
    CHECK(!current_loop_backend_start_bootstrap_probe(0u, 895u));
    CHECK(!current_loop_backend_start_bootstrap_probe(5001u, 895u));
    CHECK(!current_loop_backend_start_bootstrap_probe(UINT32_MAX, 895u));
    CHECK(!current_loop_backend_start_bootstrap_probe(1u, 756u));
    CHECK(!current_loop_backend_start_bootstrap_probe(1u, 1060u));
    check_zero();
    CHECK(probe_stages == 0u);
    CHECK(current_loop_backend_start_bootstrap_probe(5000u, 757u));
    CHECK(!current_loop_backend_start_bootstrap_probe(1u, 895u));
    CHECK(current_loop_backend_stop());
    CHECK(current_loop_backend_start_bootstrap_probe(1u, 1059u));
    CHECK(current_loop_backend_stop());
    check_zero();
}
static void expiry_and_restart(void)
{
    begin_probe(1u);
    for (unsigned i = 0; i < 19u; ++i)
    {
        timer_tick(true);
        CHECK(state().active);
    }
    timer_tick(true);
    CHECK(!state().active);
    CHECK(!state().bootstrap_probe_active);
    CHECK(state().bootstrap_probe_completed);
    CHECK(state().fault_flags == 0u);
    CHECK(probe_stages == 1u);
    check_zero();
    CHECK(current_loop_backend_stop());
    CHECK(!state().bootstrap_probe_completed);
    CHECK(current_loop_backend_set_reference_counts(30, -30));
    CHECK(current_loop_backend_start());
    timer_tick(true);
    CHECK(state().active);
    CHECK(!state().bootstrap_probe_active);
    CHECK(state().latest_output.phase_a_voltage_permille != 0);
    CHECK(current_loop_backend_stop());
    check_zero();
}
static void blocked_operations_and_stop(void)
{
    begin_probe(5000u);
    CHECK(!current_loop_backend_start());
    CHECK(!current_loop_backend_set_reference_counts(10, 10));
    CHECK(!current_loop_backend_set_rotating_reference(10, 1000u, 0u, 0u,
              CURRENT_LOOP_BACKEND_CONTROLLER_STATIONARY));
    CHECK(!current_loop_backend_set_aligned_q_reference(10, 0u, 0, 1,
                                                       mock_micros));
    CHECK(!current_loop_backend_trace_arm());
    CHECK(!current_loop_backend_reconfigure_gains(65536, 1024));
    CHECK(current_loop_backend_stop());
    CHECK(!state().active);
    CHECK(!state().bootstrap_probe_active);
    check_zero();
    CHECK(current_loop_backend_stop());
}
static void lease_expiry(void)
{
    begin_probe(5000u);
    for (unsigned i = 0; i < 500u; ++i) { timer_tick(true); }
    CHECK(state().active);
    CHECK(current_loop_backend_refresh_bootstrap_probe_vbus(895u));
    for (unsigned i = 0; i < 599u; ++i)
    {
        timer_tick(true);
        CHECK(state().active);
    }
    timer_tick(true);
    expect_fault(1u << 21);
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    initialize();
    if (strcmp(argv[1], "boundaries") == 0) { boundaries(); }
    else if (strcmp(argv[1], "expiry_restart") == 0) { expiry_and_restart(); }
    else if (strcmp(argv[1], "blocked_stop") == 0)
    { blocked_operations_and_stop(); }
    else if (strcmp(argv[1], "lease") == 0) { lease_expiry(); }
    else if (strcmp(argv[1], "adc_loss") == 0)
    {
        begin_probe(100u);
        timer_tick(true);
        timer_tick(false);
        timer_tick(false);
        expect_fault(CURRENT_LOOP_BACKEND_FAULT_DEADLINE);
    }
    else if (strcmp(argv[1], "adc_error") == 0)
    {
        begin_probe(100u);
        adc_handler(ADC1_STATUS_DMA_ERROR, NULL, adc_context);
        expect_fault(CURRENT_LOOP_BACKEND_FAULT_ADC);
    }
    else if (strcmp(argv[1], "entry_current") == 0)
    {
        sample.current_a_raw += 11u;
        deliver_sample();
        CHECK(!current_loop_backend_start_bootstrap_probe(100u, 895u));
        check_zero();
        CHECK(probe_stages == 0u);
    }
    else if (strcmp(argv[1], "entry_adc_missing") == 0)
    {
        read_status = ADC1_STATUS_NO_SAMPLE;
        CHECK(!current_loop_backend_start_bootstrap_probe(100u, 895u));
        check_zero();
    }
    else if (strcmp(argv[1], "raw_range") == 0)
    {
        begin_probe(100u);
        sample.current_b_raw = 4096u;
        deliver_sample();
        expect_fault(PHASE_CURRENT_LOOP_FAULT_INVALID_SAMPLE);
    }
    else if (strcmp(argv[1], "overcurrent") == 0)
    {
        begin_probe(100u);
        sample.current_a_raw += 601u;
        deliver_sample();
        expect_fault(PHASE_CURRENT_LOOP_FAULT_OVERCURRENT_A);
    }
    else if (strcmp(argv[1], "bad_vbus") == 0)
    {
        begin_probe(100u);
        CHECK(!current_loop_backend_refresh_bootstrap_probe_vbus(1060u));
        expect_fault(1u << 21);
    }
    else if (strcmp(argv[1], "stage_failure") == 0)
    {
        CHECK(current_loop_backend_start_bootstrap_probe(100u, 895u));
        probe_stage_ok = false;
        deliver_sample();
        expect_fault(CURRENT_LOOP_BACKEND_FAULT_PWM);
    }
    else { CHECK(false); }
    printf("bootstrap backend %s passed\n", argv[1]);
    return EXIT_SUCCESS;
}
