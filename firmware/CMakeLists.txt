set(FIRMWARE_TARGET mks57d)

set(FUTURE_CONTROL_SOURCES
    src/app/step_direction.c
    src/control/current_controller.c
)

# Keep future input and PMSM-oriented control primitives warning-clean without
# restoring the retired general application shell or a second firmware path.
add_library(mks57d_future_control OBJECT ${FUTURE_CONTROL_SOURCES})
target_include_directories(mks57d_future_control PRIVATE include)
target_compile_features(mks57d_future_control PRIVATE c_std_11)
target_compile_options(mks57d_future_control PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=softfp
    -ffunction-sections
    -fdata-sections
    -fno-common
    -Wall
    -Wextra
    -Wpedantic
    -Werror=implicit-function-declaration
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Release>:-Os>
)

set(PROJECT_SOURCES
    src/main.c
    src/app/app_state.c
    src/app/command_service.c
    src/app/diagnostics.c
    src/app/rotor_control_runtime.c
    src/board/board.c
    src/board/board_inputs.c
    src/control/aligned_torque_controller.c
    src/control/alignment_controller.c
    src/control/angle_tracker.c
    src/control/electrical_phase_predictor.c
    src/control/motor_alignment.c
    src/control/motion_profile.c
    src/control/phase_current_loop.c
    src/control/phase_current_reference.c
    src/control/pi_controller.c
    src/control/position_controller.c
    src/control/rotating_current_test.c
    src/control/velocity_controller.c
    src/drivers/adc_calibration.c
    src/drivers/adc_display.c
    src/drivers/dma_ring.c
    src/drivers/mt6816.c
    src/drivers/ssd1306.c
    src/drivers/user_inputs.c
    src/platform/adc1.c
    src/platform/cycle_counter.c
    src/platform/current_loop_backend.c
    src/platform/configuration_flash.c
    src/platform/i2c1.c
    src/platform/spi1.c
    src/platform/tim2_current_trigger.c
    src/platform/tim3_bridge_pwm.c
    src/platform/interrupt_priority.c
    src/platform/panic.c
    src/platform/runtime_profile.c
    src/platform/runtime.c
    src/platform/system.c
    src/platform/timebase.c
    src/platform/usart1_rs485.c
    src/platform/watchdog.c
    src/protocol/native_protocol.c
    src/safety/boot_self_test.c
    src/safety/encoder_liveness.c
    src/safety/watchdog_policy.c
    src/services/configuration_store.c
)

set(NATIONS_SOURCES
    vendor/nations/device/startup/startup_n32l40x_gcc.s
)

add_executable(${FIRMWARE_TARGET} ${PROJECT_SOURCES} ${NATIONS_SOURCES})
set_target_properties(${FIRMWARE_TARGET} PROPERTIES SUFFIX ".elf")

target_include_directories(${FIRMWARE_TARGET} PRIVATE
    include
    vendor/nations/cmsis/core/include
    vendor/nations/device/include
)

# N32L40X selects the Cortex-M4F device definition. Project-owned startup keeps
# the first image on reset-default 4 MHz MSI and does not configure HSI/HSE/PLL.
target_compile_definitions(${FIRMWARE_TARGET} PRIVATE
    N32L40X
    MKS57D_PRODUCT_IMAGE=1
    MKS57D_FIRMWARE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MKS57D_FIRMWARE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MKS57D_FIRMWARE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

add_custom_target(future-control-arm DEPENDS mks57d_future_control)

target_compile_features(${FIRMWARE_TARGET} PRIVATE c_std_11)
target_compile_options(${FIRMWARE_TARGET} PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=softfp
    $<$<COMPILE_LANGUAGE:C>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:C>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:C>:-fno-common>
    $<$<COMPILE_LANGUAGE:C>:-Wall>
    $<$<COMPILE_LANGUAGE:C>:-Wextra>
    $<$<COMPILE_LANGUAGE:C>:-Wpedantic>
    $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Release>:-Os>
)

# The ADC/DMA current-control path has a hard 20 kHz deadline, and each
# accepted encoder sample releases the complete PendSV estimator/controller
# chain. Keep both paths optimized in diagnostic builds; -O2 appears after the
# target-wide -Og and therefore governs these translation units.
set_source_files_properties(
    src/app/rotor_control_runtime.c
    src/control/aligned_torque_controller.c
    src/control/alignment_controller.c
    src/control/angle_tracker.c
    src/control/electrical_phase_predictor.c
    src/control/motor_alignment.c
    src/control/motion_profile.c
    src/control/phase_current_loop.c
    src/control/phase_current_reference.c
    src/control/pi_controller.c
    src/control/position_controller.c
    src/control/velocity_controller.c
    src/drivers/mt6816.c
    src/platform/adc1.c
    src/platform/current_loop_backend.c
    src/platform/spi1.c
    src/platform/tim2_current_trigger.c
    src/platform/tim3_bridge_pwm.c
    src/platform/timebase.c
    PROPERTIES COMPILE_OPTIONS -O2
)

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/n32l406cbl7.ld)
target_link_options(${FIRMWARE_TARGET} PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=softfp
    -nostartfiles
    --specs=nano.specs
    --specs=nosys.specs
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    # Keep the silent-on-boot TX entry point available for debugger/adapter
    # bring-up before a foreground protocol parser owns it.
    -Wl,--undefined=rs485_write
    -Wl,--cref
    -Wl,-Map=$<TARGET_FILE_DIR:${FIRMWARE_TARGET}>/${FIRMWARE_TARGET}.map
    -Wl,--print-memory-usage
)
target_link_libraries(${FIRMWARE_TARGET} PRIVATE c m gcc nosys)
set_property(TARGET ${FIRMWARE_TARGET} APPEND PROPERTY LINK_DEPENDS ${LINKER_SCRIPT})

add_custom_command(TARGET ${FIRMWARE_TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
        $<TARGET_FILE:${FIRMWARE_TARGET}>
        $<TARGET_FILE_DIR:${FIRMWARE_TARGET}>/${FIRMWARE_TARGET}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:${FIRMWARE_TARGET}>
        $<TARGET_FILE_DIR:${FIRMWARE_TARGET}>/${FIRMWARE_TARGET}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${FIRMWARE_TARGET}>
    COMMAND ${CMAKE_COMMAND}
        -DELF=$<TARGET_FILE:${FIRMWARE_TARGET}>
        -DNM=${CMAKE_NM}
        -DOBJDUMP=${CMAKE_OBJDUMP}
        -P ${PROJECT_SOURCE_DIR}/cmake/verify_firmware.cmake
    COMMENT "Generating HEX/BIN images and reporting image size"
)
