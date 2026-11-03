/*
 * user/start.S - User-Space ELF Program Entry Stub
 *
 * Extracts argc and argv from the initial user stack, aligns the stack to
 * a 16-byte boundary per System V AMD64 ABI, and invokes main(argc, argv).
 * Falls back to user_main() for backward compatibility if main is not defined.
 * Exits cleanly via SYS_EXIT with the program return code.
 */

.global _start
.weak main
.weak user_main

.section .text._start
.align 16

_start:
    /*
     * Initial stack layout constructed by kernel:
     *   (%rsp)     = argc (64-bit integer)
     *   8(%rsp)    = argv[0] (pointer to first argument string)
     *   ...
     *   (8 + 8*argc)(%rsp) = NULL (argv terminator)
     */
    movq (%rsp), %rdi       /* arg 1: argc */
    leaq 8(%rsp), %rsi      /* arg 2: argv */

    /* Ensure 16-byte stack alignment before function call per AMD64 ABI */
    andq $-16, %rsp

    /* Check if main is defined; if not, fall back to legacy user_main */
    movq $main, %rax
    testq %rax, %rax
    jnz 1f
    movq $user_main, %rax
1:
    call *%rax

    /* Exit with status code from return value (%rax) */
    movq %rax, %rdi
    xorq %rax, %rax         /* SYS_EXIT = 0 */
    int $0x80

.Luser_halt:
    jmp .Luser_halt
