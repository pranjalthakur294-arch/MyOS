/*
 * elf_image.S - Embedded User-Space ELF Binary
 *
 * Uses GNU Assembler .incbin directive to embed the compiled ELF64 binary
 * into the kernel .rodata section.
 */

.section .rodata
.global _binary_test_program_elf_start
.global _binary_test_program_elf_end
.global _binary_test_program_elf_size

.align 16
_binary_test_program_elf_start:
    .incbin "build/user/test_program.elf"
_binary_test_program_elf_end:

.align 8
_binary_test_program_elf_size:
    .quad _binary_test_program_elf_end - _binary_test_program_elf_start
