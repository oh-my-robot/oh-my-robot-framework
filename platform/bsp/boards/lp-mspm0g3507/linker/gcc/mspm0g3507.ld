/*****************************************************************************
 * MSPM0G3507 GCC linker script
 *
 * Symbols match startup_mspm0g350x_gcc.c (TI MSPM0 SDK).
 * Memory layout from CCS device_linker.cmd (SysConfig generated).
 *****************************************************************************/

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 128K
    SRAM  (rwx) : ORIGIN = 0x20200000, LENGTH =  32K
}

__StackTop = ORIGIN(SRAM) + LENGTH(SRAM);
end = __StackTop;

SECTIONS
{
    .intvecs :
    {
        KEEP(*(.intvecs))
    } > FLASH

    /* OM init call tables — 每级一段，按级别顺序排列（级别顺序由链接器保证，见 core/om_init.h） */
    .om_init_0 :
    {
        . = ALIGN(4);
        PROVIDE(__om_init_0_start = .);
        KEEP(*(.om_init_0))
        PROVIDE(__om_init_0_end = .);
        . = ALIGN(4);
    } > FLASH
    .om_init_1 :
    {
        . = ALIGN(4);
        PROVIDE(__om_init_1_start = .);
        KEEP(*(.om_init_1))
        PROVIDE(__om_init_1_end = .);
        . = ALIGN(4);
    } > FLASH
    .om_init_2 :
    {
        . = ALIGN(4);
        PROVIDE(__om_init_2_start = .);
        KEEP(*(.om_init_2))
        PROVIDE(__om_init_2_end = .);
        . = ALIGN(4);
    } > FLASH
    .om_init_3 :
    {
        . = ALIGN(4);
        PROVIDE(__om_init_3_start = .);
        KEEP(*(.om_init_3))
        PROVIDE(__om_init_3_end = .);
        . = ALIGN(4);
    } > FLASH
    .om_init_4 :
    {
        . = ALIGN(4);
        PROVIDE(__om_init_4_start = .);
        KEEP(*(.om_init_4))
        PROVIDE(__om_init_4_end = .);
        . = ALIGN(4);
    } > FLASH
    .om_init_5 :
    {
        . = ALIGN(4);
        PROVIDE(__om_init_5_start = .);
        KEEP(*(.om_init_5))
        PROVIDE(__om_init_5_end = .);
        . = ALIGN(4);
    } > FLASH
    .om_init_6 :
    {
        . = ALIGN(4);
        PROVIDE(__om_init_6_start = .);
        KEEP(*(.om_init_6))
        PROVIDE(__om_init_6_end = .);
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > FLASH

    .rodata : { *(.rodata*) } > FLASH

    .ARM.extab   : { *(.ARM.extab*) } > FLASH
    .ARM.exidx   : { *(.ARM.exidx*) } > FLASH

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
        KEEP(*(.fini_array*))
        KEEP(*(SORT(.fini_array.*)))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /* initialized data: FLASH LMA, SRAM VMA */
    __data_load__ = LOADADDR(.data);

    .data :
    {
        __data_start__ = .;
        *(.data*)
        __data_end__ = .;
    } > SRAM AT > FLASH

    /* RAM functions (TI .TI.ramfunc equivalent) */
    .ramfunc :
    {
        __ramfunct_load__  = LOADADDR(.ramfunc);
        __ramfunct_start__ = .;
        *(.ramfunc*)
        *(.TI.ramfunc*)
        __ramfunct_end__   = .;
    } > SRAM AT > FLASH

    /* BSS */
    .bss :
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        __bss_end__ = .;
    } > SRAM

    /* stack (minimum 512B per CCS .cmd, 2KB here) */
    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + 0x800;
    } > SRAM
}
