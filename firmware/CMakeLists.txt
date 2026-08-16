set(FIRMWARE_TARGET mks57d)

set(PROJECT_SOURCES
    src/main.c
    src/app/app_state.c
    src/app/command_service.c
    src/app/diagnostics.c
    src/board/board.c
    src/drivers/adc_sample.c
    src/drivers/dma_ring.c
    src/drivers/mt6816.c
    src/drivers/ssd1306.c
    src/platform/adc1.c
    src/platform/i2c1.c
    src/platform/spi1.c
    src/platform/interrupt_priority.c
    src/platform/panic.c
    src/platform/runtime.c
    src/platform/system.c
    src/platform/timebase.c
    src/platform/usart1_rs485.c
    src/platform/watchdog.c
    src/protocol/native_protocol.c
    src/safety/boot_self_test.c
    src/safety/fault_latch.c
    src/safety/watchdog_policy.c
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
    MKS57D_FIRMWARE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MKS57D_FIRMWARE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MKS57D_FIRMWARE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

target_compile_features(${FIRMWARE_TARGET} PRIVATE c_std_11)
target_compile_options(${FIRMWARE_TARGET} PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfloat-abi=soft
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

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/n32l406cbl7.ld)
target_link_options(${FIRMWARE_TARGET} PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfloat-abi=soft
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
