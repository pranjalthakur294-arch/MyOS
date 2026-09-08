#include "pmm.h"
#include "multiboot.h"
#include "vga.h"

/*
 * Linker script boundary symbols.
 * kernel_start is at 1 MiB (0x100000).
 * kernel_end is aligned to 4 KiB after .bss (encompasses code, data, page tables,
 * initial kernel stack, and the static PMM bitmap itself).
 */
extern uint8_t kernel_start[];
extern uint8_t kernel_end[];

/*
 * Static frame bitmap stored in BSS.
 * Each bit represents one 4 KiB physical frame:
 *   bit = 0 : FREE
 *   bit = 1 : USED / RESERVED
 */
static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];

/* Accounting counters */
static uint64_t total_managed_frames = 0;
static uint64_t total_memory_bytes = 0;
static uint64_t free_frames = 0;
static uint64_t used_frames = 0;

/*
 * Bitmap internal operations
 */

static inline void bitmap_set(uint64_t frame) {
    pmm_bitmap[frame / 8] |= (uint8_t)(1 << (frame % 8));
}

static inline void bitmap_clear(uint64_t frame) {
    pmm_bitmap[frame / 8] &= (uint8_t)~(1 << (frame % 8));
}

static inline int bitmap_test(uint64_t frame) {
    return (pmm_bitmap[frame / 8] & (1 << (frame % 8))) != 0;
}

/*
 * pmm_reserve_region - Marks a range of physical memory as USED.
 * Safely updates free_frames and used_frames only for frames that were
 * previously free, preventing double-counting.
 */
static void pmm_reserve_region(uint64_t base, uint64_t length) {
    if (length == 0) {
        return;
    }

    uint64_t start_frame = base / PAGE_SIZE;
    uint64_t end_frame = (base + length + PAGE_SIZE - 1) / PAGE_SIZE;

    if (end_frame > total_managed_frames) {
        end_frame = total_managed_frames;
    }

    for (uint64_t f = start_frame; f < end_frame; f++) {
        if (!bitmap_test(f)) {
            bitmap_set(f);
            free_frames--;
            used_frames++;
        }
    }
}

/*
 * pmm_init - Initializes the physical frame allocator using the Multiboot memory map.
 */
void pmm_init(uint64_t multiboot_info_addr) {
    if (multiboot_info_addr == 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Multiboot information structure not provided!\n");
        return;
    }

    struct multiboot_info *mbi = (struct multiboot_info *)multiboot_info_addr;

    /* Verify that the bootloader passed a valid memory map */
    if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP)) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("[FAIL] Multiboot memory map flag not set!\n");
        return;
    }

    uint64_t mmap_addr = (uint64_t)mbi->mmap_addr;
    uint64_t mmap_length = (uint64_t)mbi->mmap_length;
    uint64_t mmap_end = mmap_addr + mmap_length;

    /* Step 1: Scan memory map to find the highest usable physical address */
    uint64_t max_phys_addr = 0;
    struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)mmap_addr;

    while ((uint64_t)entry < mmap_end) {
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t region_end = entry->addr + entry->len;
            if (region_end > max_phys_addr) {
                max_phys_addr = region_end;
            }
        }
        /* Advance to next entry using entry->size + 4 */
        entry = (struct multiboot_mmap_entry *)((uint64_t)entry + entry->size + 4);
    }

    /* Clamp to supported physical limit (1 GiB) */
    if (max_phys_addr > PMM_MAX_PHYSICAL_MEMORY) {
        max_phys_addr = PMM_MAX_PHYSICAL_MEMORY;
    }

    total_managed_frames = max_phys_addr / PAGE_SIZE;
    total_memory_bytes = total_managed_frames * PAGE_SIZE;

    /* Step 2: Initially mark ALL frames up to total_managed_frames as USED (1) */
    uint64_t bitmap_bytes_used = (total_managed_frames + 7) / 8;
    for (uint64_t i = 0; i < bitmap_bytes_used; i++) {
        pmm_bitmap[i] = 0xFF;
    }
    used_frames = total_managed_frames;
    free_frames = 0;

    /* Step 3: Parse Multiboot memory map and mark usable RAM regions as FREE (0) */
    entry = (struct multiboot_mmap_entry *)mmap_addr;
    while ((uint64_t)entry < mmap_end) {
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            /* Align start up to 4 KiB and end down to 4 KiB */
            uint64_t aligned_start = (entry->addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t aligned_end = (entry->addr + entry->len) & ~(PAGE_SIZE - 1);

            if (aligned_end > max_phys_addr) {
                aligned_end = max_phys_addr;
            }

            if (aligned_end > aligned_start) {
                uint64_t start_f = aligned_start / PAGE_SIZE;
                uint64_t end_f = aligned_end / PAGE_SIZE;

                for (uint64_t f = start_f; f < end_f; f++) {
                    if (f < total_managed_frames && bitmap_test(f)) {
                        bitmap_clear(f);
                        free_frames++;
                        used_frames--;
                    }
                }
            }
        }
        entry = (struct multiboot_mmap_entry *)((uint64_t)entry + entry->size + 4);
    }

    /* Step 4: Re-reserve all regions occupied by hardware and the OS */

    /* A. Low memory 0x00000000 - 0x00100000 (1 MiB):
     * Protects real-mode IVT, BDA, EBDA, VGA text buffer at 0xB8000, and BIOS.
     * Guarantees frame 0 is permanently reserved (0 safely indicates allocation failure).
     */
    pmm_reserve_region(0x0, 0x100000);

    /* B. Kernel Image:
     * From kernel_start (1 MiB) to kernel_end (aligned boundary after .bss).
     * Encompasses .text, .rodata, .data, .bss, page tables, kernel stack, and pmm_bitmap.
     */
    uint64_t kstart = (uint64_t)kernel_start;
    uint64_t kend = (uint64_t)kernel_end;
    pmm_reserve_region(kstart, kend - kstart);

    /* C. Multiboot Information structure and Memory Map buffer */
    pmm_reserve_region(multiboot_info_addr, sizeof(struct multiboot_info));
    pmm_reserve_region(mmap_addr, mmap_length);

    /* Step 5: Print verification milestones */
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Multiboot memory map parsed\n");
    vga_puts("[OK] Physical memory manager initialized\n");
    vga_puts("[OK] 4 KiB frame allocator ready\n");
}

/*
 * pmm_alloc_frame - Allocates a single 4 KiB physical frame.
 *
 * Returns:
 *   64-bit physical address of the allocated frame, or 0 if out of memory.
 */
uint64_t pmm_alloc_frame(void) {
    uint64_t bitmap_bytes = (total_managed_frames + 7) / 8;

    for (uint64_t i = 0; i < bitmap_bytes; i++) {
        if (pmm_bitmap[i] != 0xFF) {
            for (int bit = 0; bit < 8; bit++) {
                uint64_t frame = i * 8 + bit;
                if (frame >= total_managed_frames) {
                    return 0;
                }
                if (!(pmm_bitmap[i] & (1 << bit))) {
                    bitmap_set(frame);
                    free_frames--;
                    used_frames++;
                    return frame * PAGE_SIZE;
                }
            }
        }
    }
    return 0; /* Out of memory */
}

/*
 * pmm_free_frame - Frees a previously allocated 4 KiB physical frame.
 *
 * Parameters:
 *   physical_address - 4 KiB aligned physical memory address to free.
 */
void pmm_free_frame(uint64_t physical_address) {
    /* 1. Ensure 4 KiB alignment */
    if (physical_address % PAGE_SIZE != 0) {
        return;
    }

    uint64_t frame = physical_address / PAGE_SIZE;

    /* 2. Validate bounds (frame 0 is permanently reserved and cannot be freed) */
    if (frame == 0 || frame >= total_managed_frames) {
        return;
    }

    /* 3. Validate that the frame is currently marked USED (prevent double-free) */
    if (!bitmap_test(frame)) {
        return;
    }

    /* 4. Mark frame as FREE */
    bitmap_clear(frame);
    free_frames++;
    used_frames--;
}

/*
 * Accounting accessor functions
 */

uint64_t pmm_get_total_memory(void) {
    return total_memory_bytes;
}

uint64_t pmm_get_free_memory(void) {
    return free_frames * PAGE_SIZE;
}

uint64_t pmm_get_used_memory(void) {
    return used_frames * PAGE_SIZE;
}

uint64_t pmm_get_total_frames(void) {
    return total_managed_frames;
}

uint64_t pmm_get_free_frames(void) {
    return free_frames;
}

uint64_t pmm_get_used_frames(void) {
    return used_frames;
}
