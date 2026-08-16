add_executable(host_unit_tests
    test_main.c
    ../firmware/src/app/app_state.c
    ../firmware/src/drivers/adc_sample.c
    ../firmware/src/drivers/dma_ring.c
    ../firmware/src/drivers/mt6816.c
    ../firmware/src/drivers/ssd1306.c
    ../firmware/src/safety/boot_self_test.c
    ../firmware/src/safety/fault_latch.c
    ../firmware/src/safety/watchdog_policy.c
)

target_include_directories(host_unit_tests PRIVATE ../firmware/include)
target_compile_features(host_unit_tests PRIVATE c_std_11)

if(MSVC)
    target_compile_options(host_unit_tests PRIVATE /W4 /WX)
else()
    target_compile_options(host_unit_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME host_unit_tests COMMAND host_unit_tests)
