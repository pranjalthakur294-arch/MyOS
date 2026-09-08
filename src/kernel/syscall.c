/*
 * syscall.c - System Call Implementation & Dispatcher (Stage 8B)
 *
 * Implements the C dispatcher, sys_write, sys_gettime, user memory validation,
 * and the Ring 3 system call verification harness.
 */

#include "syscall.h"
#include "idt.h"
#include "vmm.h"
#include "vga.h"
#include "timer.h"
#include "user.h"
#include "process.h"
#include <stddef.h>
#include <stdbool.h>

static void kmemcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static void kmemset(void *dest, uint8_t val, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0; i < n; i++) {
        d[i] = val;
    }
}

/*
 * syscall_init - Registers the syscall handler on IDT vector 0x80.
 * Sets gate type to 64-bit User Interrupt Gate (DPL 3, Present 1).
 */
void syscall_init(void) {
    idt_set_gate(IDT_USER_RETURN_VECTOR, isr_syscall, 0x08, IDT_FLAG_USER_INTERRUPT_GATE);
}

/*
 * syscall_validate_user_buffer_in_pml4 - Memory validator for Ring 3 pointers against
 * a specific process PML4 address space.
 *
 * Guarantees:
 *   1. Rejects NULL pointers.
 *   2. Rejects integer arithmetic overflow (start + len < start).
 *   3. Rejects addresses outside user address space [0x60000000, 0x60002000).
 *   4. Validates all touched 4 KiB pages via leaf page table flags in target PML4.
 *   5. Ensures all touched pages have PTE_PRESENT and PTE_USER set.
 *   6. Strictly forbids access to supervisor/kernel memory or unmapped pages.
 *
 * Returns:
 *   true if the entire range [ptr, ptr + len) is safely accessible by user mode.
 *   false if any part of the range is invalid or violates protection.
 */
bool syscall_validate_user_buffer_in_pml4(uint64_t pml4_phys, const void *ptr, size_t len) {
    if (ptr == NULL || pml4_phys == 0) {
        return false;
    }

    uint64_t start = (uint64_t)ptr;

    if (len == 0) {
        /* Zero-length write with non-NULL pointer: ensure address is in user space */
        if (start < USER_CODE_VADDR || start >= USER_STACK_TOP) {
            return false;
        }
        return true;
    }

    /* Prevent arithmetic overflow */
    uint64_t end = start + len;
    if (end <= start) {
        return false;
    }

    /* Range check: must reside completely within user virtual address space */
    if (start < USER_CODE_VADDR || end > USER_STACK_TOP) {
        return false;
    }

    /* Page-by-page architectural mapping and permission verification */
    uint64_t page_start = start & ~(VMM_PAGE_SIZE - 1);
    uint64_t page_end = (end - 1) & ~(VMM_PAGE_SIZE - 1);

    for (uint64_t page = page_start; page <= page_end; page += VMM_PAGE_SIZE) {
        uint64_t flags = 0;
        if (vmm_get_page_flags_in_pml4(pml4_phys, page, &flags) != 0) {
            return false; /* Page not mapped in page tables */
        }
        if (!(flags & PTE_PRESENT)) {
            return false; /* Page not marked present */
        }
        if (!(flags & PTE_USER)) {
            return false; /* Supervisor/kernel page protection violation */
        }
    }

    return true;
}

/*
 * syscall_validate_user_buffer - Validates user buffer against CURRENT PROCESS address space.
 */
bool syscall_validate_user_buffer(const void *ptr, size_t len) {
    uint64_t cr3 = vmm_read_cr3() & PTE_ADDR_MASK;
    process_t *curr = process_current();
    if (curr && curr->cr3 != 0) {
        cr3 = curr->cr3 & PTE_ADDR_MASK;
    }
    return syscall_validate_user_buffer_in_pml4(cr3, ptr, len);
}

/*
 * syscall_validate_writable_user_buffer_in_pml4 - Validates that buffer is writable by user in PML4.
 * Fails if any page is read-only (such as the user code page).
 */
bool syscall_validate_writable_user_buffer_in_pml4(uint64_t pml4_phys, const void *ptr, size_t len) {
    if (ptr == NULL || pml4_phys == 0) {
        return false;
    }

    uint64_t start = (uint64_t)ptr;

    if (len == 0) {
        if (start < USER_CODE_VADDR || start >= USER_STACK_TOP) {
            return false;
        }
        return true;
    }

    uint64_t end = start + len;
    if (end <= start) {
        return false;
    }

    if (start < USER_CODE_VADDR || end > USER_STACK_TOP) {
        return false;
    }

    uint64_t page_start = start & ~(VMM_PAGE_SIZE - 1);
    uint64_t page_end = (end - 1) & ~(VMM_PAGE_SIZE - 1);

    for (uint64_t page = page_start; page <= page_end; page += VMM_PAGE_SIZE) {
        uint64_t flags = 0;
        if (vmm_get_page_flags_in_pml4(pml4_phys, page, &flags) != 0) {
            return false;
        }
        if (!(flags & PTE_PRESENT)) {
            return false;
        }
        if (!(flags & PTE_USER)) {
            return false;
        }
        if (!(flags & PTE_WRITABLE)) {
            return false; /* Write to read-only page violation (e.g. user code page!) */
        }
    }

    return true;
}

/*
 * syscall_validate_writable_user_buffer - Validates writable user buffer against CURRENT PROCESS address space.
 */
bool syscall_validate_writable_user_buffer(const void *ptr, size_t len) {
    uint64_t cr3 = vmm_read_cr3() & PTE_ADDR_MASK;
    process_t *curr = process_current();
    if (curr && curr->cr3 != 0) {
        cr3 = curr->cr3 & PTE_ADDR_MASK;
    }
    return syscall_validate_writable_user_buffer_in_pml4(cr3, ptr, len);
}

/*
 * sys_write - Prints a buffer to the screen through the kernel VGA subsystem.
 *
 * Parameters:
 *   user_buffer - Pointer to character buffer in user memory.
 *   length      - Number of bytes to print.
 *
 * Returns:
 *   Number of bytes printed on success.
 *   -SYSCALL_EFAULT on invalid pointer or protection violation.
 */
int64_t sys_write(const char *user_buffer, size_t length) {
    if (user_buffer == NULL) {
        return SYSCALL_EFAULT;
    }

    if (length == 0) {
        if (!syscall_validate_user_buffer(user_buffer, 0)) {
            return SYSCALL_EFAULT;
        }
        return 0;
    }

    if (!syscall_validate_user_buffer(user_buffer, length)) {
        return SYSCALL_EFAULT;
    }

    /* Safely output validated user characters via kernel VGA subsystem */
    for (size_t i = 0; i < length; i++) {
        vga_putc(user_buffer[i]);
    }

    return (int64_t)length;
}

/*
 * sys_gettime - Returns current system uptime tick count from PIT timer.
 *
 * Returns:
 *   Current timer tick count (non-negative, monotonically increasing).
 */
int64_t sys_gettime(void) {
    return (int64_t)timer_get_ticks();
}

/*
 * sys_exit - Terminates user program execution and returns to kernel.
 */
int64_t sys_exit(int64_t status) {
    if (process_current() != NULL) {
        process_exit(status);
    }
    return 0;
}

/*
 * syscall_dispatch - Central system call dispatcher invoked from isr_syscall.
 *
 * Parameters:
 *   number - Syscall number from RAX
 *   arg1   - Argument 1 from RDI
 *   arg2   - Argument 2 from RSI
 *
 * Returns:
 *   Signed 64-bit result passed back to Ring 3 in RAX.
 */
int64_t syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2) {
    switch (number) {
        case SYS_EXIT:
            return sys_exit((int64_t)arg1);

        case SYS_WRITE:
            return sys_write((const char *)arg1, (size_t)arg2);

        case SYS_GETTIME:
            return sys_gettime();

        default:
            return SYSCALL_ENOSYS;
    }
}

/*
 * syscall_run_test - Executes the Ring 3 system call verification suite.
 *
 * Returns:
 *   0 on complete success.
 *   Negative code on failure.
 */
int syscall_run_test(void) {
    /* 1. Reset user status/data region */
    kmemset((void *)USER_STACK_VADDR, 0, (size_t)VMM_PAGE_SIZE);

    /* 2. Copy syscall test program into user code frame */
    size_t code_size = (size_t)((uint64_t)syscall_test_program_end - (uint64_t)syscall_test_program);
    if (code_size > (size_t)VMM_PAGE_SIZE) {
        code_size = (size_t)VMM_PAGE_SIZE;
    }
    kmemcpy((void *)user_get_code_phys(), (const void *)syscall_test_program, code_size);

    /* 3. Transition to Ring 3 to execute test program */
    switch_to_user_mode(USER_CODE_VADDR, USER_STACK_TOP);

    /* 4. Inspect status structure recorded from Ring 3 */
    const volatile syscall_status_t *status = (const volatile syscall_status_t *)USER_STACK_VADDR;

    /* Verify CPL == 3 */
    if (status->observed_cpl != 3) {
        return -1;
    }

    /* Verify magic marker */
    if (status->test_magic != SYSCALL_TEST_MAGIC) {
        return -2;
    }

    /* Verify SYS_GETTIME tick 1 */
    if (status->gettime_res1 < 0) {
        return -3;
    }

    /* Verify SYS_WRITE return value */
    if (status->write_res <= 0) {
        return -4;
    }

    /* Verify SYS_GETTIME tick 2 and monotonicity */
    if (status->gettime_res2 < status->gettime_res1) {
        return -5;
    }

    /* Verify invalid syscall returns SYSCALL_ENOSYS */
    if (status->invalid_syscall_res != SYSCALL_ENOSYS) {
        return -6;
    }

    /* Verify invalid pointer returns SYSCALL_EFAULT */
    if (status->invalid_ptr_res != SYSCALL_EFAULT) {
        return -7;
    }

    /* Verify zero-length write returns 0 */
    if (status->zero_len_res != 0) {
        return -8;
    }

    /* Verify hardware-saved CS and SS confirm Ring 3 */
    if ((user_saved_cs & 3) != 3 || (user_saved_cs & ~3ULL) != 0x20) {
        return -9;
    }
    if ((user_saved_ss & 3) != 3 || (user_saved_ss & ~3ULL) != 0x18) {
        return -10;
    }

    return 0;
}

/*
 * syscall_print_status - Shell command handler for 'syscalltest'.
 */
void syscall_print_status(void) {
    vga_puts("\nSystem Call Test:\n");

    int res = syscall_run_test();
    const volatile syscall_status_t *status = (const volatile syscall_status_t *)USER_STACK_VADDR;

    vga_puts("  CPL:              ");
    vga_print_dec((uint32_t)status->observed_cpl);
    vga_putc('\n');

    vga_puts("  SYS_WRITE:        ");
    if (status->write_res > 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (");
        vga_print_dec((uint32_t)status->write_res);
        vga_puts(" bytes)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  SYS_GETTIME:      ");
    if (status->gettime_res1 >= 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Ticks: ");
        vga_print_dec((uint32_t)status->gettime_res1);
        vga_puts(")\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Time monotonic:   ");
    if (status->gettime_res2 >= status->gettime_res1) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (");
        vga_print_dec((uint32_t)status->gettime_res1);
        vga_puts(" <= ");
        vga_print_dec((uint32_t)status->gettime_res2);
        vga_puts(")\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Invalid syscall:  ");
    if (status->invalid_syscall_res == SYSCALL_ENOSYS) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Returned -ENOSYS)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Invalid pointer:  ");
    if (status->invalid_ptr_res == SYSCALL_EFAULT) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Returned -EFAULT)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Result:           ");
    if (res == 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("PASSED (All Syscalls Verified)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED (Code ");
        vga_print_dec((uint32_t)-res);
        vga_puts(")\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}
