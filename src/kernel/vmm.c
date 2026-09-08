#include "vmm.h"
#include "pmm.h"
#include "vga.h"

/*
 * vmm_allocate_table_frame - Allocates a single physical frame from PMM
 * and zeroes all 512 entries (4096 bytes) before returning it.
 *
 * Returns:
 *   Virtual pointer to the zeroed page table, or NULL if out of memory.
 */
static uint64_t *vmm_allocate_table_frame(void) {
    uint64_t frame = pmm_alloc_frame();
    if (frame == 0) {
        return NULL;
    }

    /*
     * Because the first 1 GiB is identity-mapped, any frame in RAM
     * can be accessed directly as a virtual pointer.
     */
    uint64_t *table = (uint64_t *)frame;
    for (int i = 0; i < 512; i++) {
        table[i] = 0ULL;
    }

    return table;
}

static uint64_t boot_cr3 = 0;

/*
 * vmm_get_boot_cr3 - Returns the physical address of the initial boot PML4.
 */
uint64_t vmm_get_boot_cr3(void) {
    if (boot_cr3 == 0) {
        boot_cr3 = vmm_read_cr3() & PTE_ADDR_MASK;
    }
    return boot_cr3;
}

/*
 * vmm_init - Initializes the Virtual Memory Manager.
 * Discovers the active PML4 table from the CPU's CR3 register.
 */
void vmm_init(void) {
    uint64_t cr3 = vmm_read_cr3();
    boot_cr3 = cr3 & PTE_ADDR_MASK;

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Virtual memory manager initialized (CR3: ");
    vga_print_hex(boot_cr3);
    vga_puts(")\n");
}

/*
 * vmm_map_page - Maps a 4 KiB virtual page to a 4 KiB physical frame.
 *
 * Parameters:
 *   virtual_address  - 4 KiB aligned virtual address to map.
 *   physical_address - 4 KiB aligned physical frame to map to.
 *   flags            - Permissions (e.g. PTE_PRESENT | PTE_WRITABLE).
 *
 * Returns:
 *    0 on success.
 *   -1 if addresses are unaligned or non-canonical.
 *   -2 if out of memory allocating intermediate page tables.
 *   -3 if virtual address conflicts with an existing 2 MiB huge page.
 *   -4 if virtual address is already mapped (prevent silent overwrite).
 */
int vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags) {
    /* 1. Validate 4 KiB alignment */
    if ((virtual_address & (VMM_PAGE_SIZE - 1)) != 0 ||
        (physical_address & (VMM_PAGE_SIZE - 1)) != 0) {
        return -1;
    }

    /* 2. Validate canonical 48-bit addressing (bits 48..63 must match bit 47) */
    uint64_t high_bits = virtual_address >> 47;
    if (high_bits != 0 && high_bits != 0x1FFFFULL) {
        return -1;
    }

    /* 3. Obtain active PML4 root pointer from CR3 */
    uint64_t pml4_phys = vmm_read_cr3() & PTE_ADDR_MASK;
    uint64_t *pml4 = (uint64_t *)pml4_phys;

    /* 4. Level 4: PML4 -> PDPT */
    uint64_t pml4_idx = PML4_INDEX(virtual_address);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) {
        uint64_t *new_pdpt = vmm_allocate_table_frame();
        if (!new_pdpt) {
            return -2;
        }
        pml4[pml4_idx] = ((uint64_t)new_pdpt & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
    } else if (flags & PTE_USER) {
        pml4[pml4_idx] |= PTE_USER;
    }
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & PTE_ADDR_MASK);

    /* 5. Level 3: PDPT -> PD */
    uint64_t pdpt_idx = PDPT_INDEX(virtual_address);
    if (pdpt[pdpt_idx] & PTE_HUGE) {
        return -3; /* 1 GiB huge page collision */
    }
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        uint64_t *new_pd = vmm_allocate_table_frame();
        if (!new_pd) {
            return -2;
        }
        pdpt[pdpt_idx] = ((uint64_t)new_pd & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
    } else if (flags & PTE_USER) {
        pdpt[pdpt_idx] |= PTE_USER;
    }
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    /* 6. Level 2: PD -> PT */
    uint64_t pd_idx = PD_INDEX(virtual_address);
    if (pd[pd_idx] & PTE_HUGE) {
        return -3; /* 2 MiB huge page collision (e.g. boot identity map) */
    }
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        uint64_t *new_pt = vmm_allocate_table_frame();
        if (!new_pt) {
            return -2;
        }
        pd[pd_idx] = ((uint64_t)new_pt & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
    } else if (flags & PTE_USER) {
        pd[pd_idx] |= PTE_USER;
    }
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & PTE_ADDR_MASK);

    /* 7. Level 1: PT -> 4 KiB Physical Page */
    uint64_t pt_idx = PT_INDEX(virtual_address);
    if (pt[pt_idx] & PTE_PRESENT) {
        return -4; /* Page already mapped */
    }

    pt[pt_idx] = (physical_address & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK) | PTE_PRESENT;

    /* 8. Invalidate TLB for the modified virtual page */
    vmm_invlpg(virtual_address);

    return 0;
}

/*
 * vmm_unmap_page - Unmaps a 4 KiB virtual page and flushes the TLB entry.
 *
 * Parameters:
 *   virtual_address - 4 KiB aligned virtual address to unmap.
 *
 * Returns:
 *    0 on success.
 *   -1 if virtual address is unaligned.
 *   -2 if the virtual page is not mapped.
 *   -3 if the page resides in a 2 MiB huge page.
 */
int vmm_unmap_page(uint64_t virtual_address) {
    if ((virtual_address & (VMM_PAGE_SIZE - 1)) != 0) {
        return -1;
    }

    uint64_t pml4_phys = vmm_read_cr3() & PTE_ADDR_MASK;
    uint64_t *pml4 = (uint64_t *)pml4_phys;

    uint64_t pml4_idx = PML4_INDEX(virtual_address);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) {
        return -2;
    }
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & PTE_ADDR_MASK);

    uint64_t pdpt_idx = PDPT_INDEX(virtual_address);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        return -2;
    }
    if (pdpt[pdpt_idx] & PTE_HUGE) {
        return -3;
    }
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    uint64_t pd_idx = PD_INDEX(virtual_address);
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        return -2;
    }
    if (pd[pd_idx] & PTE_HUGE) {
        return -3;
    }
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & PTE_ADDR_MASK);

    uint64_t pt_idx = PT_INDEX(virtual_address);
    if (!(pt[pt_idx] & PTE_PRESENT)) {
        return -2;
    }

    /* Clear the PTE */
    pt[pt_idx] = 0ULL;

    /* Invalidate TLB */
    vmm_invlpg(virtual_address);

    return 0;
}

/*
 * vmm_get_mapping_in_pml4 - Resolves the physical address mapped to a virtual address
 * in an arbitrary PML4 table.
 *
 * Parameters:
 *   pml4_phys        - Physical address of target PML4 table.
 *   virtual_address  - Virtual address to inspect.
 *   physical_address - Output pointer for resolved physical address.
 *
 * Returns:
 *    0 on success.
 *   -1 if unmapped or invalid input.
 */
int vmm_get_mapping_in_pml4(uint64_t pml4_phys, uint64_t virtual_address, uint64_t *physical_address) {
    if (!physical_address || !pml4_phys) {
        return -1;
    }

    uint64_t *pml4 = (uint64_t *)(pml4_phys & PTE_ADDR_MASK);

    uint64_t pml4_idx = PML4_INDEX(virtual_address);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) {
        return -1;
    }
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & PTE_ADDR_MASK);

    uint64_t pdpt_idx = PDPT_INDEX(virtual_address);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        return -1;
    }
    if (pdpt[pdpt_idx] & PTE_HUGE) {
        /* 1 GiB huge page */
        *physical_address = (pdpt[pdpt_idx] & 0x000FFFFFC0000000ULL) + (virtual_address & 0x3FFFFFFFULL);
        return 0;
    }
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    uint64_t pd_idx = PD_INDEX(virtual_address);
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        return -1;
    }
    if (pd[pd_idx] & PTE_HUGE) {
        /* 2 MiB huge page */
        *physical_address = (pd[pd_idx] & 0x000FFFFFFFE00000ULL) + (virtual_address & 0x1FFFFFULL);
        return 0;
    }
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & PTE_ADDR_MASK);

    uint64_t pt_idx = PT_INDEX(virtual_address);
    if (!(pt[pt_idx] & PTE_PRESENT)) {
        return -1;
    }

    /* 4 KiB page */
    *physical_address = (pt[pt_idx] & PTE_ADDR_MASK) + PAGE_OFFSET(virtual_address);
    return 0;
}

/*
 * vmm_get_mapping - Resolves the physical address mapped to a virtual address in current CR3.
 */
int vmm_get_mapping(uint64_t virtual_address, uint64_t *physical_address) {
    return vmm_get_mapping_in_pml4(vmm_read_cr3(), virtual_address, physical_address);
}

/*
 * vmm_get_page_flags_in_pml4 - Returns the leaf architectural PTE flags for a virtual address
 * in an arbitrary PML4 table.
 *
 * Parameters:
 *   pml4_phys       - Physical address of target PML4 table.
 *   virtual_address - Virtual address to inspect.
 *   flags           - Output pointer for flags bits.
 *
 * Returns:
 *    0 on success.
 *   -1 if unmapped or invalid input.
 */
int vmm_get_page_flags_in_pml4(uint64_t pml4_phys, uint64_t virtual_address, uint64_t *flags) {
    if (!flags || !pml4_phys) {
        return -1;
    }

    uint64_t *pml4 = (uint64_t *)(pml4_phys & PTE_ADDR_MASK);

    uint64_t pml4_idx = PML4_INDEX(virtual_address);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) {
        return -1;
    }
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & PTE_ADDR_MASK);

    uint64_t pdpt_idx = PDPT_INDEX(virtual_address);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        return -1;
    }
    if (pdpt[pdpt_idx] & PTE_HUGE) {
        *flags = pdpt[pdpt_idx] & ~PTE_ADDR_MASK;
        return 0;
    }
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    uint64_t pd_idx = PD_INDEX(virtual_address);
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        return -1;
    }
    if (pd[pd_idx] & PTE_HUGE) {
        *flags = pd[pd_idx] & ~PTE_ADDR_MASK;
        return 0;
    }
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & PTE_ADDR_MASK);

    uint64_t pt_idx = PT_INDEX(virtual_address);
    if (!(pt[pt_idx] & PTE_PRESENT)) {
        return -1;
    }

    *flags = pt[pt_idx] & ~PTE_ADDR_MASK;
    return 0;
}

/*
 * vmm_get_page_flags - Returns the leaf architectural PTE flags for a virtual address in current CR3.
 */
int vmm_get_page_flags(uint64_t virtual_address, uint64_t *flags) {
    return vmm_get_page_flags_in_pml4(vmm_read_cr3(), virtual_address, flags);
}

/*
 * vmm_run_test - End-to-end VMM demonstration test:
 *   1. Allocates 1 physical frame from PMM.
 *   2. Maps VMM_TEST_VIRTUAL_ADDRESS (0x40000000) to the physical frame.
 *   3. Queries and verifies mapping via vmm_get_mapping().
 *   4. Writes a 64-bit test pattern through the virtual address.
 *   5. Reads it back through the virtual address and validates match.
 *   6. Cross-verifies by reading via the physical identity mapping.
 *   7. Unmaps the virtual page and invalidates TLB.
 *   8. Releases the physical frame back to PMM.
 *
 * Returns:
 *   0 on success, negative error code on failure.
 */
int vmm_run_test(void) {
    /* 1. Allocate physical frame */
    uint64_t phys_frame = pmm_alloc_frame();
    if (phys_frame == 0) {
        return -1;
    }

    /* 2. Map test virtual address */
    int map_res = vmm_map_page(VMM_TEST_VIRTUAL_ADDRESS, phys_frame, PTE_PRESENT | PTE_WRITABLE);
    if (map_res != 0) {
        pmm_free_frame(phys_frame);
        return -2;
    }

    /* 3. Verify mapping query */
    uint64_t resolved_phys = 0;
    if (vmm_get_mapping(VMM_TEST_VIRTUAL_ADDRESS, &resolved_phys) != 0 || resolved_phys != phys_frame) {
        vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS);
        pmm_free_frame(phys_frame);
        return -3;
    }

    /* 4. Write test pattern through mapped virtual address */
    const uint64_t TEST_MAGIC = 0xCAFEBABE12345678ULL;
    volatile uint64_t *virt_ptr = (volatile uint64_t *)VMM_TEST_VIRTUAL_ADDRESS;
    *virt_ptr = TEST_MAGIC;

    /* 5. Read back through virtual address */
    if (*virt_ptr != TEST_MAGIC) {
        vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS);
        pmm_free_frame(phys_frame);
        return -4;
    }

    /* 6. Cross-verify reading through physical identity map */
    volatile uint64_t *identity_ptr = (volatile uint64_t *)phys_frame;
    if (*identity_ptr != TEST_MAGIC) {
        vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS);
        pmm_free_frame(phys_frame);
        return -5;
    }

    /* 7. Unmap test virtual page */
    if (vmm_unmap_page(VMM_TEST_VIRTUAL_ADDRESS) != 0) {
        pmm_free_frame(phys_frame);
        return -6;
    }

    /* 8. Free physical frame */
    pmm_free_frame(phys_frame);

    return 0;
}

/*
 * vmm_create_process_pml4 - Creates a new PML4 table for a process.
 *
 * Establishes the necessary supervisor kernel mappings:
 *   - Clones PML4[0] / PDPT[0] (0..1 GiB identity mapping, supervisor-only).
 *   - Clones kernel heap mapping (PD[128], supervisor-only).
 *
 * Parameters:
 *   out_pml4_phys    - Output pointer for allocated PML4 root frame.
 *   tables           - Array to record allocated page table frames for cleanup.
 *   table_count      - In/out pointer tracking count of recorded table frames.
 *   max_tables       - Maximum capacity of tables array.
 *
 * Returns:
 *   0 on success, negative on error.
 */
int vmm_create_process_pml4(uint64_t *out_pml4_phys, uint64_t *tables, size_t *table_count, size_t max_tables) {
    if (!out_pml4_phys || !tables || !table_count || *table_count >= max_tables) {
        return -1;
    }

    uint64_t boot_pml4_phys = vmm_get_boot_cr3();
    uint64_t *boot_pml4 = (uint64_t *)boot_pml4_phys;

    /* 1. Allocate process PML4 frame */
    uint64_t *proc_pml4 = vmm_allocate_table_frame();
    if (!proc_pml4) {
        return -2;
    }
    uint64_t proc_pml4_phys = (uint64_t)proc_pml4;
    *out_pml4_phys = proc_pml4_phys;

    /* 2. Allocate process PDPT frame for PML4[0] */
    uint64_t *proc_pdpt = vmm_allocate_table_frame();
    if (!proc_pdpt) {
        pmm_free_frame(proc_pml4_phys);
        return -2;
    }
    uint64_t proc_pdpt_phys = (uint64_t)proc_pdpt;
    tables[(*table_count)++] = proc_pdpt_phys;

    /* Set PML4[0] with User bit (leaf/intermediate permissions will restrict kernel pages) */
    proc_pml4[0] = proc_pdpt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;

    /* 3. Link Kernel Identity Map (0..1 GiB) into proc_pdpt[0] */
    uint64_t *boot_pdpt = (uint64_t *)(boot_pml4[0] & PTE_ADDR_MASK);
    proc_pdpt[0] = boot_pdpt[0]; /* Points to shared boot_pd_table (supervisor-only) */

    /* 4. Allocate per-process PD frame for 1 GiB..2 GiB (PDPT[1]) */
    if (*table_count >= max_tables) {
        return -3;
    }
    uint64_t *proc_pd = vmm_allocate_table_frame();
    if (!proc_pd) {
        return -2;
    }
    uint64_t proc_pd_phys = (uint64_t)proc_pd;
    tables[(*table_count)++] = proc_pd_phys;
    proc_pdpt[1] = proc_pd_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;

    /* 5. Link Kernel Heap (0x50000000) from boot PD */
    if (boot_pdpt[1] & PTE_PRESENT) {
        uint64_t *boot_pd1 = (uint64_t *)(boot_pdpt[1] & PTE_ADDR_MASK);
        /* Entry 128 (0x80) covers 0x50000000..0x501FFFFF (kernel heap) */
        proc_pd[128] = boot_pd1[128]; /* Shared kernel heap PT (supervisor-only) */
    }

    return 0;
}

/*
 * vmm_map_page_in_pml4 - Maps a 4 KiB virtual page in a specified PML4 root.
 *
 * Parameters:
 *   pml4_phys        - Physical address of target PML4 table.
 *   virtual_address  - 4 KiB aligned virtual address to map.
 *   physical_address - 4 KiB aligned physical frame to map to.
 *   flags            - Permissions (e.g. PTE_PRESENT | PTE_USER).
 *   tables           - Array tracking allocated intermediate table frames.
 *   table_count      - In/out pointer tracking count of recorded table frames.
 *   max_tables       - Capacity of tables array.
 *
 * Returns:
 *   0 on success, negative on error.
 */
int vmm_map_page_in_pml4(uint64_t pml4_phys, uint64_t virtual_address, uint64_t physical_address, uint64_t flags,
                         uint64_t *tables, size_t *table_count, size_t max_tables) {
    if ((virtual_address & (VMM_PAGE_SIZE - 1)) != 0 ||
        (physical_address & (VMM_PAGE_SIZE - 1)) != 0) {
        return -1;
    }

    uint64_t high_bits = virtual_address >> 47;
    if (high_bits != 0 && high_bits != 0x1FFFFULL) {
        return -1;
    }

    uint64_t *pml4 = (uint64_t *)(pml4_phys & PTE_ADDR_MASK);

    /* Level 4: PML4 -> PDPT */
    uint64_t pml4_idx = PML4_INDEX(virtual_address);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) {
        if (!tables || !table_count || *table_count >= max_tables) return -2;
        uint64_t *new_pdpt = vmm_allocate_table_frame();
        if (!new_pdpt) return -2;
        tables[(*table_count)++] = (uint64_t)new_pdpt;
        pml4[pml4_idx] = ((uint64_t)new_pdpt & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
    } else if (flags & PTE_USER) {
        pml4[pml4_idx] |= PTE_USER;
    }
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & PTE_ADDR_MASK);

    /* Level 3: PDPT -> PD */
    uint64_t pdpt_idx = PDPT_INDEX(virtual_address);
    if (pdpt[pdpt_idx] & PTE_HUGE) {
        return -3;
    }
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        if (!tables || !table_count || *table_count >= max_tables) return -2;
        uint64_t *new_pd = vmm_allocate_table_frame();
        if (!new_pd) return -2;
        tables[(*table_count)++] = (uint64_t)new_pd;
        pdpt[pdpt_idx] = ((uint64_t)new_pd & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
    } else if (flags & PTE_USER) {
        pdpt[pdpt_idx] |= PTE_USER;
    }
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    /* Level 2: PD -> PT */
    uint64_t pd_idx = PD_INDEX(virtual_address);
    if (pd[pd_idx] & PTE_HUGE) {
        return -3;
    }
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        if (!tables || !table_count || *table_count >= max_tables) return -2;
        uint64_t *new_pt = vmm_allocate_table_frame();
        if (!new_pt) return -2;
        tables[(*table_count)++] = (uint64_t)new_pt;
        pd[pd_idx] = ((uint64_t)new_pt & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
    } else if (flags & PTE_USER) {
        pd[pd_idx] |= PTE_USER;
    }
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & PTE_ADDR_MASK);

    /* Level 1: PT -> 4 KiB Physical Page */
    uint64_t pt_idx = PT_INDEX(virtual_address);
    if (pt[pt_idx] & PTE_PRESENT) {
        return -4; /* Already mapped */
    }

    pt[pt_idx] = (physical_address & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK) | PTE_PRESENT;

    /* Invalidate TLB if mapping in current address space */
    if ((vmm_read_cr3() & PTE_ADDR_MASK) == (pml4_phys & PTE_ADDR_MASK)) {
        vmm_invlpg(virtual_address);
    }

    return 0;
}
