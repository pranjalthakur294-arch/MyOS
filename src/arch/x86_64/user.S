/*
 * user.S - Low-level User Mode (Ring 3) Transition Routine and Test Program
 *
 * Implements the architecture-mandated IRETQ privilege transition from
 * CPL 0 (Ring 0) to CPL 3 (Ring 3) and vector 0x80 return trap.
 */

.section .data
.align 8
.global kernel_caller_rsp
.global user_saved_cs
.global user_saved_rip
.global user_saved_rflags
.global user_saved_rsp
.global user_saved_ss

kernel_caller_rsp: .quad 0
user_saved_cs:     .quad 0
user_saved_rip:    .quad 0
user_saved_rflags: .quad 0
user_saved_rsp:    .quad 0
user_saved_ss:     .quad 0

.section .text
.code64

.global switch_to_user_mode
.type switch_to_user_mode, @function

/*
 * switch_to_user_mode - Transitions execution to Ring 3 (CPL 3).
 *
 * Parameters (System V AMD64 ABI):
 *   %rdi - User entry point RIP
 *   %rsi - User stack top RSP
 *
 * Frame layout constructed on kernel stack:
 *   [rsp + 32]: SS     (0x1B = User Data Selector 0x18 | RPL 3)
 *   [rsp + 24]: RSP    (User stack pointer from %rsi)
 *   [rsp + 16]: RFLAGS (0x202 = IF enabled)
 *   [rsp +  8]: CS     (0x23 = User Code Selector 0x20 | RPL 3)
 *   [rsp +  0]: RIP    (User instruction pointer from %rdi)
 */
switch_to_user_mode:
    /* Preserve caller RFLAGS and callee-saved registers on kernel stack */
    pushfq
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Save current kernel %rsp so isr_user_return can resume cleanly */
    movabsq $kernel_caller_rsp, %rax
    movq %rsp, (%rax)

    /* Reload data segment registers with User Data Selector (0x1B) */
    movw $0x1B, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /* Push SS (User Data Selector with RPL 3) */
    pushq $0x1B

    /* Push RSP (User stack top) */
    pushq %rsi

    /* Push RFLAGS with Interrupt Flag (IF, bit 9) enabled */
    pushfq
    popq %rax
    orq $0x200, %rax
    pushq %rax

    /* Push CS (User Code Selector with RPL 3) */
    pushq $0x23

    /* Push RIP (User entry point) */
    pushq %rdi

    /* Atomically transition privilege to CPL 3 */
    iretq

/*
 * isr_user_return - Handles vector 0x80 trap from user mode back to Ring 0.
 */
.global isr_user_return
.type isr_user_return, @function

isr_user_return:
    /* Record hardware-pushed frame values from TSS.rsp0 stack */
    movq 0(%rsp), %rax       /* User RIP */
    movq 8(%rsp), %rbx       /* User CS */
    movq 16(%rsp), %rcx      /* User RFLAGS */
    movq 24(%rsp), %rdx      /* User RSP */
    movq 32(%rsp), %rsi      /* User SS */

    movabsq $user_saved_rip, %rdi
    movq %rax, (%rdi)
    movabsq $user_saved_cs, %rdi
    movq %rbx, (%rdi)
    movabsq $user_saved_rflags, %rdi
    movq %rcx, (%rdi)
    movabsq $user_saved_rsp, %rdi
    movq %rdx, (%rdi)
    movabsq $user_saved_ss, %rdi
    movq %rsi, (%rdi)

    /* Clean up the 5-quadword hardware frame on TSS.rsp0 */
    addq $40, %rsp

    /* Restore kernel data segment registers (0x10) */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* Restore kernel caller stack pointer saved before switch_to_user_mode */
    movabsq $kernel_caller_rsp, %rax
    movq (%rax), %rsp

    /* Restore callee-saved registers and caller RFLAGS */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popfq
    ret

/*
 * user_test_program - Minimal test program executing entirely in Ring 3.
 *
 * It reads the CS register, verifies CPL == 3, records CPL and canary to user memory,
 * executes an iteration loop, and traps back to Ring 0 via vector 0x80.
 */
.global user_test_program
.global user_test_program_end
.type user_test_program, @function

user_test_program:
    /* 1. Read CS selector into RAX */
    movw %cs, %ax
    movzwq %ax, %rax
    andq $3, %rax

    /* 2. Write observed CPL (3) to user status memory (0x60001000) */
    movabsq $0x60001000, %rdx
    movq %rax, (%rdx)

    /* 3. Write canary value 0x1337BEEF to 0x60001008 */
    movabsq $0x1337BEEF, %rbx
    movq %rbx, 8(%rdx)

    /* 4. Execute a loop incrementing iteration counter at 0x60001010 */
    movq $1000, %rcx
.user_loop:
    incq 16(%rdx)
    decq %rcx
    jnz .user_loop

    /* 5. Trap back to Ring 0 via vector 0x80 */
    int $0x80

    /* Safety halt loop */
1:  jmp 1b

user_test_program_end:
