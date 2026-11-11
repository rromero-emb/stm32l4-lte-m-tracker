/* STM32L476RG: 1 MB flash, 96 KB SRAM1, 32 KB SRAM2.
 * SRAM2 (0x10000000) keeps its contents across a system reset (unless the
 * SRAM2_RST option bit says otherwise), so the crash record lives there. */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    SRAM1 (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
    SRAM2 (rw)  : ORIGIN = 0x10000000, LENGTH = 32K
}

_estack = ORIGIN(SRAM1) + LENGTH(SRAM1);
_min_stack_size = 0x1000; /* checked below: at least 4 KB left for the stack */

SECTIONS
{
    .isr_vector : ALIGN(4)
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text : ALIGN(4)
    {
        *(.text*)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx : { *(.ARM.exidx*) } > FLASH

    _sidata = LOADADDR(.data);

    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM1 AT > FLASH

    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM1

    /* Not initialised by the startup code: survives a reset. */
    .noinit (NOLOAD) : ALIGN(4)
    {
        *(.noinit*)
    } > SRAM2

    _end = _ebss;   /* heap start for _sbrk (newlib-nano), if ever used */

    ASSERT(_ebss + _min_stack_size <= _estack, "SRAM1 overflow: not enough room for the stack")

    /DISCARD/ : { *(.ARM.attributes) }
}
