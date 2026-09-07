/*
 * interrupts.S - Low-level x86-64 Interrupt Service Routine (ISR) stubs
 *
 * Each stub preserves the full CPU register state, ensures the System V ABI
 * stack alignment and direction flag constraints, calls the corresponding C
 * handler, and returns via iretq.
 */

.section .text
.code64

.global isr_test_wrapper
.type isr_test_wrapper, @function

/* External C handler defined in kernel.c */
.extern test_interrupt_handler

isr_test_wrapper:
    /*
     * When this wrapper is entered, the CPU has already automatically
     * pushed 5 quadwords (40 bytes) onto the stack:
     *   [SS], [RSP], [RFLAGS], [CS], [RIP]
     *
     * We push 15 general-purpose registers (120 bytes).
     * Total stack delta = 40 + 120 = 160 bytes (a multiple of 16).
     * This guarantees 16-byte stack alignment before the C function call.
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

    /* System V AMD64 ABI requires the Direction Flag to be cleared */
    cld

    /* Invoke the high-level C test interrupt handler */
    call test_interrupt_handler

    /* Restore all general-purpose registers in reverse order */
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
     * and resumes the interrupted instruction sequence.
     */
    iretq
