if(NOT DEFINED ELF OR NOT EXISTS "${ELF}")
    message(FATAL_ERROR "Firmware ELF does not exist: ${ELF}")
endif()

foreach(tool IN ITEMS NM OBJDUMP)
    if(NOT DEFINED ${tool} OR NOT EXISTS "${${tool}}")
        message(FATAL_ERROR "Required firmware verification tool is missing: ${tool}")
    endif()
endforeach()

execute_process(
    COMMAND "${NM}" -n "${ELF}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE nm_output
    ERROR_VARIABLE nm_error
)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "arm-none-eabi-nm failed: ${nm_error}")
endif()

function(require_symbol symbol expected)
    string(REGEX MATCH "(^|\n)([0-9A-Fa-f]+)[ \t]+[^ \t\r\n]+[ \t]+${symbol}([\r\n]|$)"
           symbol_match "${nm_output}")
    if(NOT symbol_match)
        message(FATAL_ERROR "Required firmware symbol is missing: ${symbol}")
    endif()

    string(TOLOWER "${CMAKE_MATCH_2}" actual)
    string(TOLOWER "${expected}" wanted)
    if(NOT actual STREQUAL wanted)
        message(FATAL_ERROR
            "Firmware symbol ${symbol} is 0x${actual}; expected 0x${wanted}")
    endif()
endfunction()

require_symbol(_estack 20004000)
require_symbol(__StackTop 20004000)
require_symbol(__StackLimit 20003800)
require_symbol(__sram2_start__ 20006000)
require_symbol(__sram2_end__ 20008000)

execute_process(
    COMMAND "${NM}" -S --defined-only "${ELF}"
    RESULT_VARIABLE nm_sizes_result
    OUTPUT_VARIABLE nm_sizes_output
    ERROR_VARIABLE nm_sizes_error
)
if(NOT nm_sizes_result EQUAL 0)
    message(FATAL_ERROR "arm-none-eabi-nm size query failed: ${nm_sizes_error}")
endif()

string(REGEX MATCH
       "(^|\n)[0-9A-Fa-f]+[ \t]+([0-9A-Fa-f]+)[ \t]+[^ \t\r\n]+[ \t]+g_diagnostics([\r\n]|$)"
       diagnostics_symbol_match "${nm_sizes_output}")
if(NOT diagnostics_symbol_match)
    message(FATAL_ERROR "Required firmware symbol is missing: g_diagnostics")
endif()
string(TOLOWER "${CMAKE_MATCH_2}" diagnostics_size)
if(NOT diagnostics_size STREQUAL "00000034" AND
   NOT diagnostics_size STREQUAL "34")
    message(FATAL_ERROR
        "g_diagnostics is 0x${diagnostics_size} bytes; expected 0x34")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -s -j .isr_vector "${ELF}"
    RESULT_VARIABLE objdump_result
    OUTPUT_VARIABLE vector_output
    ERROR_VARIABLE objdump_error
)
if(NOT objdump_result EQUAL 0)
    message(FATAL_ERROR "arm-none-eabi-objdump failed: ${objdump_error}")
endif()

# The first vector word is little-endian 0x20004000.
if(NOT vector_output MATCHES "00400020")
    message(FATAL_ERROR "Vector table does not contain the SRAM1 stack top")
endif()

message(STATUS "Verified split SRAM map, initial vector stack pointer, and diagnostic ABI symbol")
