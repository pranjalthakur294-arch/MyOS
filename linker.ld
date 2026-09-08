/*
 * linker.ld - Linker script for MyOS (x86-64)
 *
 * This script instructs the linker how to arrange the sections of the kernel
 * in the final ELF64 binary.
 */

OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
    /*
     * Load the kernel at 1 MiB (0x100000) in physical memory.
     * The first 1 MiB is reserved for real-mode IVT, BIOS data area (BDA),
     * VGA text video memory (0xB8000), and bootloader data.
     */
    . = 1M;
    kernel_start = .;

    /*
     * Multiboot Header Section
     * The Multiboot specification requires headers to be located in the first
     * 8 KiB of the binary file and 4-byte / 8-byte aligned.
     */
    .multiboot : ALIGN(4K)
    {
        KEEP(*(.multiboot))
    }

    /* Executable machine instructions (.text) */
    .text : ALIGN(4K)
    {
        *(.text .text.*)
    }

    /* Read-only data (.rodata): strings, constant tables, GDT */
    .rodata : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    /* Initialized read-write data (.data) */
    .data : ALIGN(4K)
    {
        *(.data .data.*)
    }

    /* Uninitialized read-write data (.bss) and stack */
    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    . = ALIGN(4K);
    kernel_end = .;

    /* Discard metadata sections not needed in a bare-metal kernel */
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.*)
    }
}
