#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * x86-64 Page Table Entry (PTE) Architectural Flags
 */
#define PTE_PRESENT   (1ULL << 0)   /* Page is present in memory */
#define PTE_WRITABLE  (1ULL << 1)   /* Read/write permission (0 = read-only, 1 = read/write) */
#define PTE_USER      (1ULL << 2)   /* User/supervisor permission (0 = supervisor, 1 = user) */
#define PTE_PWT       (1ULL << 3)   /* Page-level write-through */
#define PTE_PCD       (1ULL << 4)   /* Page-level cache disable */
#define PTE_ACCESSED  (1ULL << 5)   /* Set by hardware on read/write */
#define PTE_DIRTY     (1ULL << 6)   /* Set by hardware on write (leaf PTE only) */
#define PTE_HUGE      (1ULL << 7)   /* 2 MiB (PD) or 1 GiB (PDPT) page size */
#define PTE_GLOBAL    (1ULL << 8)   /* Global translation (survives CR3 reload) */

/* Mask to extract 4 KiB-aligned physical address (bits 12..51) */
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

/* Standard page size */
#define VMM_PAGE_SIZE 4096ULL

/*
 * Virtual Address Translation Breakdown (Canonical 48-bit addressing)
 * Bits 47..39: PML4 index (9 bits -> 512 entries)
 * Bits 38..30: PDPT index (9 bits -> 512 entries)
 * Bits 29..21: PD index   (9 bits -> 512 entries)
 * Bits 20..12: PT index   (9 bits -> 512 entries)
 * Bits 11..0 : Offset     (12 bits -> 4096 bytes)
 */
#define PML4_INDEX(va)  (((uint64_t)(va) >> 39) & 0x1FFULL)
#define PDPT_INDEX(va)  (((uint64_t)(va) >> 30) & 0x1FFULL)
#define PD_INDEX(va)    (((uint64_t)(va) >> 21) & 0x1FFULL)
#define PT_INDEX(va)    (((uint64_t)(va) >> 12) & 0x1FFULL)
#define PAGE_OFFSET(va) ((uint64_t)(va) & 0xFFFULL)

/*
 * VMM-Managed Virtual Address Space
 * 0x00000000 .. 0x3FFFFFFF is the existing boot identity mapping (first 1 GiB, 2 MiB pages).
 * 0x40000000 .. 0x7FFFFFFF is the VMM-managed 4 KiB page region (PDPT[1], 1 GiB to 2 GiB).
 */
#define VMM_MANAGED_START          0x40000000ULL
#define VMM_MANAGED_END            0x80000000ULL
#define VMM_TEST_VIRTUAL_ADDRESS   0x40000000ULL

/*
 * Inline Assembly Register & TLB Helpers
 */

/* Reads active PML4 physical base address from CR3 */
static inline uint64_t vmm_read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/* Loads CR3 with a new PML4 base address (flushes entire TLB) */
static inline void vmm_write_cr3(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
}

/* Reads faulting linear address from CR2 (valid inside #PF handler) */
static inline uint64_t vmm_read_cr2(void) {
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

/* Invalidates cached TLB translation for a single virtual page */
static inline void vmm_invlpg(uint64_t virtual_address) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

/*
 * Public VMM API
 */
void vmm_init(void);

int vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
int vmm_unmap_page(uint64_t virtual_address);
int vmm_get_mapping(uint64_t virtual_address, uint64_t *physical_address);
int vmm_get_page_flags(uint64_t virtual_address, uint64_t *flags);
int vmm_run_test(void);

/* Stage 9 Process Address Space API */
uint64_t vmm_get_boot_cr3(void);
int vmm_get_mapping_in_pml4(uint64_t pml4_phys, uint64_t virtual_address, uint64_t *physical_address);
int vmm_get_page_flags_in_pml4(uint64_t pml4_phys, uint64_t virtual_address, uint64_t *flags);
int vmm_create_process_pml4(uint64_t *out_pml4_phys, uint64_t *tables, size_t *table_count, size_t max_tables);
int vmm_map_page_in_pml4(uint64_t pml4_phys, uint64_t virtual_address, uint64_t physical_address, uint64_t flags,
                         uint64_t *tables, size_t *table_count, size_t max_tables);

#endif /* VMM_H */
