/*
 * boot.S - Entry point for MyOS (x86-64)
 *
 * Responsibilities:
 * 1. Expose Multiboot 1 & Multiboot 2 headers so standard bootloaders
 *    (GRUB, QEMU direct kernel boot) recognize and load the kernel.
 * 2. Receive control in 32-bit Protected Mode at physical address 1 MiB.
 * 3. Verify CPUID and x86-64 Long Mode capability on the host CPU.
 * 4. Set up an initial 4-level paging structure (PML4, PDPT, PD) identity-mapping
 *    the first 1 GiB of physical memory using 2 MiB huge pages.
 * 5. Enable PAE, activate Long Mode in IA32_EFER MSR, and enable paging in CR0.
 * 6. Load a 64-bit Global Descriptor Table (GDT64).
 * 7. Far jump into 64-bit Long Mode.
 * 8. Load 64-bit data segment registers and establish the kernel stack pointer.
 * 9. Call kernel_main() in C.
 */

/* Multiboot 1 Constants */
.set MB1_MAGIC,    0x1BADB002
.set MB1_FLAGS,    (1 << 0) | (1 << 1)   /* Align loaded modules on page boundaries + provide mmap */
.set MB1_CHECKSUM, -(MB1_MAGIC + MB1_FLAGS)

/* Multiboot 2 Constants */
.set MB2_MAGIC,    0xE85250D6
.set MB2_ARCH,     0                     /* 0 = i386 32-bit protected mode */

/* -------------------------------------------------------------------------
 * Multiboot Headers Section
 * Must reside within the first 8 KiB of the executable file.
 * ------------------------------------------------------------------------- */
.section .multiboot, "a"
.align 4
mb1_header:
    .long MB1_MAGIC
    .long MB1_FLAGS
    .long MB1_CHECKSUM

.align 8
mb2_header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long (mb2_header_end - mb2_header_start)
    .long -(MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header_start))

    /* Multiboot 2 End Tag */
    .short 0    /* Type: 0 = termination */
    .short 0    /* Flags: 0 */
    .long 8     /* Size: 8 bytes */
mb2_header_end:

/* -------------------------------------------------------------------------
 * 32-bit Protected Mode Boot Code
 * ------------------------------------------------------------------------- */
.section .text
.code32
.global _start
.type _start, @function

_start:
    /* Disable interrupts during early processor mode transition */
    cli

    /* Initialize temporary 32-bit stack pointer */
    movl $stack_top, %esp

    /* Preserve Multiboot magic (EAX) and Multiboot information pointer (EBX) */
    pushl %ebx
    pushl %eax

    /* Step 1: Verify CPUID instruction is supported by the CPU */
    call check_cpuid

    /* Step 2: Verify CPU supports 64-bit Long Mode */
    call check_long_mode

    /* Step 3: Populate 4-level page tables for identity-mapping first 1 GiB */
    call setup_page_tables

    /* Step 4: Enable PAE (Physical Address Extension) in CR4 bit 5 */
    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4

    /* Step 5: Load Page Map Level 4 (PML4) base address into CR3 */
    movl $pml4_table, %eax
    movl %eax, %cr3

    /* Step 6: Enable Long Mode (LME bit 8) in IA32_EFER MSR (0xC0000080) */
    movl $0xC0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr

    /* Step 7: Enable Paging (PG bit 31) and Protected Mode (PE bit 0) in CR0 */
    movl %cr0, %eax
    orl $((1 << 31) | (1 << 0)), %eax
    movl %eax, %cr0

    /* Step 8: Load 64-bit Global Descriptor Table (GDT64) */
    lgdt gdt64_pointer

    /* Restore Multiboot arguments before jumping to 64-bit code */
    popl %eax                   /* Multiboot magic */
    popl %ebx                   /* Multiboot information pointer */

    /* Step 9: Perform far jump to 64-bit code segment (Selector 0x08) */
    ljmp $0x08, $long_mode_start

/* -------------------------------------------------------------------------
 * CPU Verification Routines (32-bit Protected Mode)
 * ------------------------------------------------------------------------- */

/*
 * check_cpuid:
 * Checks if the CPUID instruction can be executed.
 * In x86, bit 21 (ID flag) in EFLAGS can only be modified if CPUID is supported.
 */
check_cpuid:
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $(1 << 21), %eax
    pushl %eax
    popfl
    pushfl
    popl %eax
    pushl %ecx
    popfl
    xorl %ecx, %eax
    jz .error_no_cpuid
    ret

.error_no_cpuid:
    /* Display red 'NO CPUID' on VGA buffer (0xB8000) and halt */
    movl $0x4F4F4F4E, 0xB8000   /* 'NO' */
    movl $0x4F434F20, 0xB8004   /* ' C' */
    movl $0x4F554F50, 0xB8008   /* 'PU' */
    movl $0x4F444F49, 0xB800C   /* 'ID' */
    cli
1:  hlt
    jmp 1b

/*
 * check_long_mode:
 * Verifies whether the processor supports x86-64 Long Mode via CPUID.
 */
check_long_mode:
    /* Check whether extended CPUID functions are available */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb .error_no_long_mode

    /* Query extended processor features */
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx      /* Bit 29: Long Mode (LM) feature bit */
    jz .error_no_long_mode
    ret

.error_no_long_mode:
    /* Display red 'NO LM' on VGA buffer (0xB8000) and halt */
    movl $0x4F4F4F4E, 0xB8000   /* 'NO' */
    movl $0x4F4D4F4C, 0xB8004   /* 'LM' */
    cli
1:  hlt
    jmp 1b

/*
 * setup_page_tables:
 * Configures an initial identity mapping covering the first 1 GiB of RAM.
 * PML4[0]  -> PDPT
 * PDPT[0]  -> PD
 * PD[0..511] -> 512 x 2 MiB huge pages (0x00000000 to 0x3FFFFFFF = 1 GiB)
 */
setup_page_tables:
    /* PML4 entry 0 points to PDPT with Present (bit 0) and Writable (bit 1) */
    movl $pdpt_table, %eax
    orl $0x03, %eax
    movl %eax, pml4_table

    /* PDPT entry 0 points to PD with Present (bit 0) and Writable (bit 1) */
    movl $pd_table, %eax
    orl $0x03, %eax
    movl %eax, pdpt_table

    /* Loop through 512 PD entries, mapping each to a 2 MiB huge page */
    movl $0, %ecx               /* Counter: 0 to 511 */
    movl $0x83, %eax            /* Base address 0x0 | Present (1) | Writable (2) | HugePage 2MB (0x80) */

.map_pd_loop:
    movl %eax, pd_table(, %ecx, 8)
    movl $0, pd_table + 4(, %ecx, 8)  /* High 32-bits are zero */
    addl $0x200000, %eax              /* Advance physical address by 2 MiB */
    incl %ecx
    cmpl $512, %ecx
    jne .map_pd_loop
    ret

/* -------------------------------------------------------------------------
 * 64-bit Long Mode Kernel Entry
 * ------------------------------------------------------------------------- */
.code64
long_mode_start:
    /* Pass Multiboot arguments to kernel_main per System V AMD64 ABI:
     * %rdi (1st parameter) = Multiboot magic (zero-extended from %eax)
     * %rsi (2nd parameter) = Multiboot information address (zero-extended from %ebx)
     */
    movl %eax, %edi
    movl %ebx, %esi

    /* Reload all data segment registers with 64-bit data segment selector (0x10) */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* Establish 64-bit stack pointer (16-byte aligned per System V AMD64 ABI) */
    movq $stack_top, %rsp

    /* Clear RFLAGS (resets direction flag DF, ensures interrupts disabled) */
    pushq $0
    popfq

    /* Call the C kernel entry point: kernel_main(magic, mbi_addr) */
    call kernel_main

    /* Safety halt loop if kernel_main ever returns */
.hang:
    cli
    hlt
    jmp .hang

/* -------------------------------------------------------------------------
 * 64-bit Global Descriptor Table (GDT)
 * ------------------------------------------------------------------------- */
.section .rodata
.align 8
gdt64:
    .quad 0x0000000000000000    /* 0x00: Null descriptor */
    /*
     * 0x08: 64-bit Kernel Code Segment Descriptor
     * Access Byte: 0x9A (Present=1, Ring=0, Type=Code, Executable=1, Readable=1)
     * Flags:       0x20 (Long Mode flag L=1, Default size D=0)
     */
    .quad 0x00209A0000000000
    /*
     * 0x10: 64-bit Kernel Data Segment Descriptor
     * Access Byte: 0x92 (Present=1, Ring=0, Type=Data, Writable=1)
     * Flags:       0x00
     */
    .quad 0x0000920000000000
gdt64_end:

gdt64_pointer:
    .word gdt64_end - gdt64 - 1 /* Table size (limit) minus 1 */
    .long gdt64                 /* 32-bit linear address of GDT (loaded before 64-bit switch) */

/* -------------------------------------------------------------------------
 * Uninitialized Data Section (BSS): Page Tables and Initial Stack
 * ------------------------------------------------------------------------- */
.section .bss
.align 4096
pml4_table:
    .skip 4096                  /* 4 KiB Page Map Level 4 table */
pdpt_table:
    .skip 4096                  /* 4 KiB Page Directory Pointer Table */
pd_table:
    .skip 4096                  /* 4 KiB Page Directory table */

.align 16
.global stack_bottom
.global stack_top
stack_bottom:
    .skip 16384                 /* 16 KiB initial kernel stack */
stack_top:
