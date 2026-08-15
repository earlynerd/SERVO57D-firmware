/**
  ****************************************************************************
  Copyright (c) 2019, Nations Technologies Inc.

  All rights reserved.
  ****************************************************************************

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  - Redistributions of source code must retain the above copyright notice,
  this list of conditions and the disclaimer below.

  Nations' name may not be used to endorse or promote products derived from
  this software without specific prior written permission.

  DISCLAIMER: THIS SOFTWARE IS PROVIDED BY NATIONS "AS IS" AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
  DISCLAIMED. IN NO EVENT SHALL NATIONS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
  OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  ****************************************************************************

  Derived from the Nations N32L40x GCC linker script. The N32L406xB SRAM is
  split into a 16 KiB SRAM1 bank and an 8 KiB SRAM2 bank with an unmapped
  8 KiB hole between them. The first diagnostic image keeps its stack and all
  ordinary sections in SRAM1. SRAM2 is initialized by project-owned startup
  code but remains unavailable for allocations until it is validated on the
  physical device.
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)   : ORIGIN = 0x08000000, LENGTH = 128K
    SRAM1 (xrw)  : ORIGIN = 0x20000000, LENGTH = 16K
    SRAM2 (xrw)  : ORIGIN = 0x20006000, LENGTH = 8K
}

/* Keep the initial stack wholly inside SRAM1. */
_estack = ORIGIN(SRAM1) + LENGTH(SRAM1);
__StackTop = _estack;
__StackLimit = __StackTop - 0x800;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text .text*)
        *(.glue_7 .glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata .rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM1 AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = .;
        *(.bss .bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = .;
    } > SRAM1

    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit .noinit*)
        . = ALIGN(4);
    } > SRAM1

    end = .;
    _end = .;

    /*
     * SRAM2 has parity protection and must be initialized before reads. This
     * NOLOAD section reserves a future placement name, while the assertion
     * below prevents the first image from allocating anything there.
     */
    .sram2 (NOLOAD) :
    {
        . = ALIGN(4);
        __sram2_start__ = .;
        *(.sram2 .sram2*)
        . = ALIGN(4);
        __sram2_allocated_end__ = .;
    } > SRAM2

    __sram2_end__ = ORIGIN(SRAM2) + LENGTH(SRAM2);

    /DISCARD/ :
    {
        *(.note.GNU-stack)
        *(.gnu_debuglink)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}

ASSERT(_estack == 0x20004000, "N32L406CBL7 SRAM1 stack top is incorrect")
ASSERT(__StackLimit == 0x20003800, "N32L406CBL7 SRAM1 stack guard is incorrect")
ASSERT(__sram2_start__ == 0x20006000, "N32L406CBL7 SRAM2 origin is incorrect")
ASSERT(__sram2_end__ == 0x20008000, "N32L406CBL7 SRAM2 end is incorrect")
ASSERT(SIZEOF(.sram2) == 0, "Initial image must not allocate objects in SRAM2")
ASSERT(_end <= __StackLimit, "N32L406CBL7 SRAM1 overflow: less than 2 KiB stack remains")
ASSERT(SIZEOF(.isr_vector) > 0, "Interrupt vector table is missing")
