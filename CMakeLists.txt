cmake_minimum_required(VERSION 3.24)

project(mks57d_open_firmware VERSION 0.32.2 LANGUAGES C)

option(MKS57D_BUILD_FIRMWARE "Build the N32L406 firmware image" OFF)
option(MKS57D_BUILD_TESTS "Build host-side unit tests" OFF)

if(MKS57D_BUILD_TESTS AND WIN32 AND MSVC AND
   ("$ENV{INCLUDE}" STREQUAL "" OR "$ENV{LIB}" STREQUAL ""))
    # A plain PowerShell session may find cl.exe on PATH without having the
    # Visual Studio C/SDK environment. Remove the exact stale test artifact so
    # a later ctest invocation cannot report yesterday's binary as today's
    # result, then point the caller at the wrapper that loads vcvars64.bat.
    file(REMOVE "${CMAKE_BINARY_DIR}/tests/host_unit_tests.exe")
    message(FATAL_ERROR
        "MSVC is visible but its INCLUDE/LIB environment is not loaded. "
        "Run: pwsh -File tools/build.ps1 -Target host-tests. To prepare the "
        "current shell instead, dot-source: . .\\tools\\import-msvc-environment.ps1")
endif()

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
