cmake_minimum_required(VERSION 3.24)

project(mks57d_open_firmware VERSION 0.17.8 LANGUAGES C)

option(MKS57D_BUILD_FIRMWARE "Build the N32L406 firmware image" OFF)
option(MKS57D_BUILD_TESTS "Build host-side unit tests" OFF)

if(MKS57D_BUILD_FIRMWARE AND MKS57D_BUILD_TESTS)
    message(FATAL_ERROR "Firmware and host tests use different compilers; configure one at a time")
endif()

if(NOT MKS57D_BUILD_FIRMWARE AND NOT MKS57D_BUILD_TESTS)
    message(FATAL_ERROR "Select MKS57D_BUILD_FIRMWARE or MKS57D_BUILD_TESTS; the supplied presets do this")
endif()

if(MKS57D_BUILD_FIRMWARE)
    enable_language(ASM)
    add_subdirectory(firmware)
endif()

if(MKS57D_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
