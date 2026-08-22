add_executable(host_unit_tests
    test_main.c
    ../firmware/src/app/app_state.c
    ../firmware/src/app/application_core.c
    ../firmware/src/app/command_service.c
    ../firmware/src/app/motion_manager.c
    ../firmware/src/app/step_direction.c
    ../firmware/src/control/aligned_torque_controller.c
    ../firmware/src/control/alignment_controller.c
    ../firmware/src/control/angle_tracker.c
    ../firmware/src/control/current_controller.c
    ../firmware/src/control/motion_profile.c
    ../firmware/src/control/motor_alignment.c
    ../firmware/src/control/pi_controller.c
    ../firmware/src/control/phase_current_loop.c
    ../firmware/src/control/phase_current_reference.c
    ../firmware/src/control/rotating_current_test.c
    ../firmware/src/control/servo_core.c
    ../firmware/src/control/velocity_controller.c
    ../firmware/src/drivers/adc_calibration.c
    ../firmware/src/drivers/adc_display.c
    ../firmware/src/drivers/adc_sample.c
    ../firmware/src/drivers/dma_ring.c
    ../firmware/src/drivers/encoder_display.c
    ../firmware/src/drivers/input_display.c
    ../firmware/src/drivers/mt6816.c
    ../firmware/src/drivers/pulse_input_display.c
    ../firmware/src/drivers/ssd1306.c
    ../firmware/src/drivers/user_inputs.c
    ../firmware/src/protocol/native_protocol.c
    ../firmware/src/safety/boot_self_test.c
    ../firmware/src/safety/fault_latch.c
    ../firmware/src/safety/watchdog_policy.c
    ../firmware/src/services/configuration_store.c
)

target_include_directories(host_unit_tests PRIVATE ../firmware/include)
target_compile_features(host_unit_tests PRIVATE c_std_11)

if(MSVC)
    target_compile_options(host_unit_tests PRIVATE /W4 /WX)
else()
    target_compile_options(host_unit_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(host_unit_tests PRIVATE m)
endif()

add_test(NAME host_unit_tests COMMAND host_unit_tests)
