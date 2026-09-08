/*
 * interrupts.S - Low-level x86-64 Interrupt Service Routine (ISR) stubs
 *
 * Each stub preserves the full CPU register state, ensures the System V ABI
 * stack alignment and direction flag constraints, calls the corresponding C
 * handler, and returns via iretq.
 */

.section .text
.code64

.global isr_timer
.type isr_timer, @function

/* External C timer and scheduler handler defined in timer.c */
.extern timer_interrupt_handler

isr_timer:
    /*
     * When this wrapper is entered via IRQ0 (vector 0x20), the CPU hardware
     * automatically pushes 5 quadwords (40 bytes) onto the current stack:
     *   [SS]       +152
     *   [RSP]      +144
     *   [RFLAGS]   +136
     *   [CS]       +128
     *   [RIP]      +120
     *
     * We push all 15 general-purpose registers (120 bytes) to capture the
     * complete, uncorrupted CPU execution state of the preempted task:
     */
    pushq %rax      /* +112 */
    pushq %rcx      /* +104 */
    pushq %rdx      /* +96  */
    pushq %rbx      /* +88  */
    pushq %rbp      /* +80  */
    pushq %rsi      /* +72  */
    pushq %rdi      /* +64  */
    pushq %r8       /* +56  */
    pushq %r9       /* +48  */
    pushq %r10      /* +40  */
    pushq %r11      /* +32  */
    pushq %r12      /* +24  */
    pushq %r13      /* +16  */
    pushq %r14      /* +8   */
    pushq %r15      /* 0    */

    /* System V AMD64 ABI requires the Direction Flag to be cleared */
    cld

    /*
     * Pass current %rsp in %rdi (1st parameter in System V AMD64 ABI).
     * %rsp points to the bottom of the complete 20-quadword interrupt frame.
     * Note: Total frame size is 40 + 120 = 160 bytes (a multiple of 16).
     * Since %rsp before interrupt was 16-byte aligned, %rsp here is 16-byte aligned.
     */
    movq %rsp, %rdi
    call timer_interrupt_handler

    /*
     * timer_interrupt_handler returns the %rsp of the task that should run next.
     * If the scheduler selected a different task, %rax is that task's saved %rsp.
     * If no context switch occurred, %rax is the same %rsp passed in %rdi.
     * We switch %rsp to the target task's stack pointer:
     */
    movq %rax, %rsp

    /* Restore all 15 general-purpose registers in reverse order from target stack */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax

    /*
     * 64-bit Interrupt Return:
     * Atomically pops RIP, CS, RFLAGS, RSP, and SS from the stack
     * and resumes execution of the target task.
     */
    iretq

.global isr_keyboard
.type isr_keyboard, @function

/* External C handler defined in keyboard.c */
.extern keyboard_handler

isr_keyboard:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    cld
    call keyboard_handler

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax

    iretq

.global isr_page_fault
.type isr_page_fault, @function

/* External C handler defined in idt.c */
.extern page_fault_handler

isr_page_fault:
    /*
     * For #PF (vector 14), the CPU automatically pushes:
     *   [SS], [RSP], [RFLAGS], [CS], [RIP], [Error Code] (48 bytes)
     *
     * We push 15 general-purpose registers (120 bytes).
     * Total stack delta so far: 48 + 120 = 168 bytes (168 % 16 == 8).
     * We subtract 8 bytes to ensure 16-byte stack alignment before the call.
     */
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    subq $8, %rsp

    /* Read CR2 into RDI (1st parameter: faulting linear address) */
    movq %cr2, %rdi

    /* Error code is at 128(%rsp) (120 bytes GPRs + 8 bytes padding) */
    movq 128(%rsp), %rsi

    cld
    call page_fault_handler

    /* Remove alignment padding */
    addq $8, %rsp

    /* Restore general-purpose registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax

    /* Pop CPU-pushed error code before returning */
    addq $8, %rsp

    iretq
