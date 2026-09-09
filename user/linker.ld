/*
 * user/linker.ld - Linker Script for User-Space ELF Executables
 *
 * Defines explicit PT_LOAD segments for code (PF_R | PF_X) and data/bss (PF_R | PF_W),
 * page-aligned to 4 KiB boundaries within the MyOS user virtual address space.
 */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5); /* PF_R | PF_X */
    data PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS
{
    . = 0x60000000;

    .text : {
        *(.text._start)
        *(.text*)
        *(.rodata*)
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data*)
    } :data

    .bss : {
        *(.bss*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
