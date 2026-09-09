/*
 * user/start.S - User-Space ELF Program Entry Stub
 *
 * Provides the _start entry point, invokes user_main, and transitions
 * cleanly to SYS_EXIT (0) via int $0x80 using the return value as exit status.
 */

.global _start
.section .text._start
.align 16

_start:
    /* Call user main function */
    call user_main

    /* Pass return code from %rax into %rdi (arg1) */
    movq %rax, %rdi

    /* Syscall number 0 = SYS_EXIT */
    xorq %rax, %rax
    int $0x80

.Luser_halt:
    jmp .Luser_halt
