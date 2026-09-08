/*
 * context_switch.S - Low-level cooperative context switch for x86-64
 *
 * Prototype (C declaration):
 *   void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
 *
 * System V AMD64 ABI Register Assignments:
 *   %rdi (1st argument): old_rsp (pointer to uint64_t where current %rsp is saved)
 *   %rsi (2nd argument): new_rsp (target stack pointer to restore into %rsp)
 *
 * Callee-saved registers per System V AMD64 ABI:
 *   %rbx, %rbp, %r12, %r13, %r14, %r15
 *
 * Stack frame mechanics:
 *   When context_switch() is entered via a C 'call' instruction, the CPU
 *   has already pushed the 8-byte return address (RIP) onto the current stack.
 *   We then push RFLAGS and all 6 callee-saved registers (7 quadwords = 56 bytes).
 *   Then we save the resulting %rsp to *old_rsp (%rdi).
 *   Next, we update %rsp to new_rsp (%rsi).
 *   Finally, we pop the 6 callee-saved registers and RFLAGS in reverse order,
 *   and execute 'ret', which pops the new task's return RIP and transfers control.
 */

.section .text
.code64

.global context_switch
.type context_switch, @function

context_switch:
    /* 1. Save RFLAGS on current stack */
    pushfq

    /* 2. Save callee-preserved general-purpose registers */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* 3. Save current stack pointer into *old_rsp */
    movq %rsp, (%rdi)

    /* 4. Switch to new task's stack pointer */
    movq %rsi, %rsp

    /* 5. Restore callee-preserved registers for the new task */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    /* 6. Restore new task's RFLAGS */
    popfq

    /* 7. Resume execution at saved RIP on the new task's stack */
    ret
