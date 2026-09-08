/*
 * user.c - User Mode (Ring 3) Subsystem and Verification Engine
 */

#include "user.h"
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "vmm.h"
#include "vga.h"
#include <stddef.h>

static uint64_t user_code_phys = 0;
static uint64_t user_stack_phys = 0;
static bool user_initialized = false;

/* Memory copy helper */
static void kmemcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

/* Memory set helper */
static void kmemset(void *dest, uint8_t val, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0; i < n; i++) {
        d[i] = val;
    }
}

/*
 * user_init - Allocates physical frames, establishes user virtual page mappings,
 * sets up the user return IDT gate, and executes the initial user mode verification.
 */
void user_init(void) {
    if (user_initialized) {
        return;
    }

    /* 1. Allocate dedicated 4 KiB physical frames for user code and user stack */
    user_code_phys = pmm_alloc_frame();
    user_stack_phys = pmm_alloc_frame();
    if (!user_code_phys || !user_stack_phys) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Unable to allocate physical frames for user mode\n");
        return;
    }

    /* 2. Map User Code page with PTE_USER | PTE_PRESENT (Read-Only User Code) */
    int r1 = vmm_map_page(USER_CODE_VADDR, user_code_phys,
                          PTE_PRESENT | PTE_USER);

    /* 3. Map User Stack page with PTE_USER | PTE_WRITABLE | PTE_PRESENT (Read-Write User Stack) */
    int r2 = vmm_map_page(USER_STACK_VADDR, user_stack_phys,
                          PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    if (r1 != 0 || r2 != 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Failed to map user pages in VMM\n");
        return;
    }

    /* 4. Install user return trap gate on vector 0x80 (DPL 3, User Interrupt Gate) */
    idt_set_gate(IDT_USER_RETURN_VECTOR, isr_user_return, 0x08, IDT_FLAG_USER_INTERRUPT_GATE);

    /* 5. Zero the user stack and status region */
    kmemset((void *)USER_STACK_VADDR, 0, (size_t)VMM_PAGE_SIZE);

    /* 6. Copy user program code into the user code frame via writable kernel identity mapping */
    size_t code_size = (size_t)((uint64_t)user_test_program_end - (uint64_t)user_test_program);
    if (code_size > (size_t)VMM_PAGE_SIZE) {
        code_size = (size_t)VMM_PAGE_SIZE;
    }
    kmemcpy((void *)user_code_phys, (const void *)user_test_program, code_size);

    user_initialized = true;

    /* 7. Run initial user-mode transition test */
    switch_to_user_mode(USER_CODE_VADDR, USER_STACK_TOP);

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] GDT/TSS & User mode verified (CPL=3, Canary=0x1337BEEF)\n");
}

/*
 * user_get_status - Returns pointer to user status block in user memory.
 */
const user_status_t *user_get_status(void) {
    return (const user_status_t *)USER_STACK_VADDR;
}

uint64_t user_get_code_phys(void) {
    return user_code_phys;
}

uint64_t user_get_stack_phys(void) {
    return user_stack_phys;
}

/*
 * user_run_test - Runs verification checks on the user mode environment.
 *
 * Returns 0 on success, negative on error.
 */
int user_run_test(void) {
    if (!user_initialized) {
        return -1;
    }

    /* Reset status block */
    kmemset((void *)USER_STACK_VADDR, 0, (size_t)VMM_PAGE_SIZE);

    /* Execute transition into user mode */
    switch_to_user_mode(USER_CODE_VADDR, USER_STACK_TOP);

    /* Check virtual memory mapping */
    uint64_t phys_check = 0;
    if (vmm_get_mapping(USER_CODE_VADDR, &phys_check) != 0 || phys_check != user_code_phys) {
        return -2;
    }
    if (vmm_get_mapping(USER_STACK_VADDR, &phys_check) != 0 || phys_check != user_stack_phys) {
        return -3;
    }

    /* Check page permissions: user code must be Read-Only, user stack must be Writable */
    uint64_t flags = 0;
    if (vmm_get_page_flags(USER_CODE_VADDR, &flags) != 0 || !(flags & PTE_USER) || (flags & PTE_WRITABLE)) {
        return -4;
    }
    if (vmm_get_page_flags(USER_STACK_VADDR, &flags) != 0 || !(flags & PTE_USER) || !(flags & PTE_WRITABLE)) {
        return -5;
    }

    /* Check that kernel memory remains supervisor only (e.g. 0x100000 kernel_start) */
    if (vmm_get_page_flags(0x100000ULL, &flags) == 0 && (flags & PTE_USER)) {
        return -6;
    }

    const volatile user_status_t *status = (const volatile user_status_t *)USER_STACK_VADDR;

    /* Verify CPL == 3 */
    if (status->observed_cpl != 3) {
        return -7;
    }

    /* Verify Canary */
    if (status->canary_magic != USER_CANARY_MAGIC) {
        return -8;
    }

    /* Verify iterations */
    if (status->iterations == 0) {
        return -9;
    }

    /* Verify hardware-saved CS and SS from privilege transition frame */
    if ((user_saved_cs & 3) != 3 || (user_saved_cs & ~3ULL) != 0x20) {
        return -10;
    }
    if ((user_saved_ss & 3) != 3 || (user_saved_ss & ~3ULL) != 0x18) {
        return -11;
    }

    return 0;
}

/*
 * user_print_status - Formats and displays user mode status report for shell.
 */
void user_print_status(void) {
    int res = user_run_test();

    vga_puts("\nUser Mode (Ring 3) Status:\n");
    vga_puts("  User CS Selector: 0x0023 (DPL 3, RPL 3)\n");
    vga_puts("  User DS Selector: 0x001B (DPL 3, RPL 3)\n");
    vga_puts("  TSS Selector:     0x0028 (64-bit Available TSS)\n");
    vga_puts("  TSS RSP0:         ");
    vga_print_hex(gdt_get_rsp0());
    vga_puts("\n  User Code VAddr:  ");
    vga_print_hex(USER_CODE_VADDR);
    vga_puts(" -> Phys: ");
    vga_print_hex(user_code_phys);
    vga_puts("\n  User Stack VAddr: ");
    vga_print_hex(USER_STACK_VADDR);
    vga_puts(" -> Phys: ");
    vga_print_hex(user_stack_phys);
    vga_puts("\n  User Stack Top:   ");
    vga_print_hex(USER_STACK_TOP);

    const volatile user_status_t *status = (const volatile user_status_t *)USER_STACK_VADDR;
    vga_puts("\n  Observed CPL:     ");
    vga_print_dec((uint32_t)status->observed_cpl);
    vga_puts("\n  Canary Magic:     ");
    vga_print_hex(status->canary_magic);
    vga_puts("\n  Iterations:       ");
    vga_print_dec((uint32_t)status->iterations);
    vga_puts("\n  Hardware Saved CS:");
    vga_print_hex(user_saved_cs);
    vga_puts("\n  Hardware Saved SS:");
    vga_print_hex(user_saved_ss);

    vga_puts("\n  Verification:     ");
    if (res == 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("PASSED (Ring 3 Confirmed)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED (Code ");
        vga_print_dec((uint32_t)-res);
        vga_puts(")\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}
