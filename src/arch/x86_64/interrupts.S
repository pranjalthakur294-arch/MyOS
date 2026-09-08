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

/* External C handler defined in timer.c */
.extern timer_handler

isr_timer:
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

    /* Invoke the high-level C timer interrupt handler */
    call timer_handler

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
