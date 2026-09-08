/*
 * user.S - Low-level User Mode (Ring 3) Transition Routine, System Call Entry, and Tests
 *
 * Implements:
 *   1. switch_to_user_mode: Transitions from CPL 0 to CPL 3 via IRETQ frame.
 *   2. isr_syscall: Handles IDT vector 0x80 traps.
 *      - Dispatches SYS_EXIT (0) to isr_user_return to resume kernel caller.
 *      - Dispatches other syscalls to syscall_dispatch, places return value in RAX,
 *        and returns to Ring 3 via iretq.
 *   3. isr_user_return: Cleans up TSS.rsp0, restores kernel registers, and returns to C.
 *   4. user_test_program: Stage 8A minimal user test program.
 *   5. syscall_test_program: Stage 8B comprehensive user syscall test program.
 *   6. user_syscall0 / user_syscall2: User-side ABI wrappers.
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

    /* Save current kernel %rsp so user exit can resume cleanly */
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
 * isr_syscall - IDT vector 0x80 System Call Entry Point.
 *
 * Hardware-pushed stack frame on TSS.rsp0:
 *   [rsp +  0]: RIP
 *   [rsp +  8]: CS
 *   [rsp + 16]: RFLAGS
 *   [rsp + 24]: RSP
 *   [rsp + 32]: SS
 */
.global isr_syscall
.type isr_syscall, @function

isr_syscall:
    /* Check if RAX is SYS_EXIT (0) - terminate user mode session */
    testq %rax, %rax
    jz isr_user_return

    /* Preserve user general-purpose registers (14 registers = 112 bytes) */
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Align stack to 16 bytes for System V AMD64 ABI (40 + 112 + 8 = 160) */
    subq $8, %rsp

    /* Prepare arguments for C dispatcher: syscall_dispatch(number, arg1, arg2) */
    movq %rsi, %rdx          /* 3rd arg: arg2 */
    movq %rdi, %rsi          /* 2nd arg: arg1 */
    movq %rax, %rdi          /* 1st arg: syscall number */

    /* Ensure kernel data segments are active during kernel C execution */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es

    /* Call C dispatcher */
    call syscall_dispatch

    /* RAX now holds the signed 64-bit return value from dispatcher */

    /* Reload user data segments */
    movw $0x1B, %cx
    movw %cx, %ds
    movw %cx, %es

    /* Remove 16-byte alignment padding */
    addq $8, %rsp

    /* Restore preserved user registers without overwriting RAX */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx

    /* Return to Ring 3 execution with syscall result in RAX */
    iretq

/*
 * isr_user_return - Handles clean termination of user mode back to Ring 0.
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
 * user_test_program - Stage 8A Minimal Ring 3 Test Program.
 *
 * Reads CS selector, verifies CPL == 3, records canary and loop iterations,
 * then calls SYS_EXIT (0) via vector 0x80 to return cleanly to the kernel.
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

    /* 5. Trap back to Ring 0 via vector 0x80 with SYS_EXIT (0) */
    xorq %rax, %rax
    int $0x80

    /* Safety halt loop */
1:  jmp 1b

user_test_program_end:

/*
 * User-Side Wrappers (executable in Ring 3)
 */
.global user_syscall0
.type user_syscall0, @function
user_syscall0:
    movq %rdi, %rax
    int $0x80
    ret

.global user_syscall2
.type user_syscall2, @function
user_syscall2:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    int $0x80
    ret

/*
 * syscall_test_program - Stage 8B Comprehensive Ring 3 System Call Test Program.
 *
 * Executed entirely at CPL 3:
 *   1. Verifies CPL == 3.
 *   2. Calls SYS_GETTIME (syscall 2) -> records tick1.
 *   3. Calls SYS_WRITE (syscall 1) -> prints greeting from user mode.
 *   4. Delay loop.
 *   5. Calls SYS_GETTIME (syscall 2) -> records tick2.
 *   6. Calls invalid syscall 999 -> records result (expect -ENOSYS = -3).
 *   7. Calls SYS_WRITE with NULL pointer -> records result (expect -EFAULT = -2).
 *   8. Calls SYS_WRITE with length 0 -> records result (expect 0).
 *   9. Writes SYSCALL_TEST_MAGIC (0x515CA115) to confirm completion.
 *  10. Calls SYS_EXIT (0) to cleanly return to kernel.
 */
.global syscall_test_program
.global syscall_test_program_end
.type syscall_test_program, @function

syscall_test_program:
    /* Base of status structure at 0x60001000 */
    movabsq $0x60001000, %r12

    /* 1. Read CS selector and verify CPL == 3 */
    movw %cs, %ax
    movzwq %ax, %rax
    andq $3, %rax
    movq %rax, 0(%r12)       /* status->observed_cpl */

    /* 2. SYS_GETTIME (syscall 2) */
    movq $2, %rax
    int $0x80
    movq %rax, 8(%r12)       /* status->gettime_res1 */

    /* 3. SYS_WRITE (syscall 1) */
    movq $1, %rax
    leaq syscall_msg(%rip), %rdi
    movq $syscall_msg_len, %rsi
    int $0x80
    movq %rax, 16(%r12)      /* status->write_res */

    /* 4. Delay loop to allow timer ticks to progress */
    movq $5000000, %rcx
.delay_loop:
    decq %rcx
    jnz .delay_loop

    /* 5. Second SYS_GETTIME (syscall 2) */
    movq $2, %rax
    int $0x80
    movq %rax, 24(%r12)      /* status->gettime_res2 */

    /* 6. Invalid Syscall (number 999) */
    movq $999, %rax
    xorq %rdi, %rdi
    xorq %rsi, %rsi
    int $0x80
    movq %rax, 32(%r12)      /* status->invalid_syscall_res */

    /* 7. Invalid Pointer SYS_WRITE (NULL pointer) */
    movq $1, %rax
    xorq %rdi, %rdi          /* NULL */
    movq $10, %rsi           /* length 10 */
    int $0x80
    movq %rax, 40(%r12)      /* status->invalid_ptr_res */

    /* 8. Zero-length SYS_WRITE (valid user pointer, len 0) */
    movq $1, %rax
    leaq syscall_msg(%rip), %rdi
    xorq %rsi, %rsi          /* length 0 */
    int $0x80
    movq %rax, 48(%r12)      /* status->zero_len_res */

    /* 9. Record completion magic: 0x515CA115 */
    movabsq $0x515CA115, %rax
    movq %rax, 56(%r12)      /* status->test_magic */

    /* 10. Terminate user mode session via SYS_EXIT (0) */
    xorq %rax, %rax
    int $0x80

    /* Safety halt loop */
2:  jmp 2b

syscall_msg:
    .ascii "  [Ring 3 User] Hello from SYS_WRITE!\n"
syscall_msg_end:
    .set syscall_msg_len, syscall_msg_end - syscall_msg

syscall_test_program_end:
