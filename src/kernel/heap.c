#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "vga.h"

/*
 * Global Heap State
 */
static struct heap_block *heap_head = NULL;
static bool heap_initialized = false;

/*
 * heap_align_size - Aligns requested allocation size up to an 8-byte boundary.
 * Handles integer overflow safely.
 *
 * Returns:
 *   Aligned size >= 8, or 0 if size is 0 or on arithmetic overflow.
 */
static inline size_t heap_align_size(size_t size) {
    if (size == 0) {
        return 0;
    }
    if (size > (size_t)(-HEAP_ALIGNMENT)) {
        return 0; /* Arithmetic overflow */
    }
    return (size + (HEAP_ALIGNMENT - 1)) & ~(HEAP_ALIGNMENT - 1);
}

/*
 * heap_init - Establishes and initializes the fixed 64 KiB kernel heap.
 *
 * Allocates 16 physical 4 KiB frames from PMM and maps them to contiguous
 * virtual addresses starting at HEAP_START (0x50000000) using VMM.
 * If any page allocation or mapping fails, all previously allocated frames
 * are freed, mapped pages are unmapped, and the heap remains uninitialized.
 */
void heap_init(void) {
    const size_t num_pages = HEAP_SIZE / VMM_PAGE_SIZE; /* 16 pages */
    uint64_t allocated_frames[16];

    for (size_t i = 0; i < num_pages; i++) {
        uint64_t virt = HEAP_START + (i * VMM_PAGE_SIZE);
        uint64_t phys = pmm_alloc_frame();
        if (phys == 0) {
            /* Out of physical memory: unmap and release previous frames */
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(HEAP_START + (j * VMM_PAGE_SIZE));
                pmm_free_frame(allocated_frames[j]);
            }
            heap_initialized = false;
            vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_puts("[ERROR] Heap init: PMM OOM\n");
            return;
        }
        allocated_frames[i] = phys;

        int res = vmm_map_page(virt, phys, PTE_PRESENT | PTE_WRITABLE);
        if (res != 0) {
            /* Mapping error: release current frame and all previous allocations */
            pmm_free_frame(phys);
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(HEAP_START + (j * VMM_PAGE_SIZE));
                pmm_free_frame(allocated_frames[j]);
            }
            heap_initialized = false;
            vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_puts("[ERROR] Heap init: VMM error\n");
            return;
        }
    }

    /* Initialize the single root free block spanning the entire heap payload */
    heap_head = (struct heap_block *)HEAP_START;
    heap_head->size = HEAP_SIZE - sizeof(struct heap_block);
    heap_head->free = true;
    heap_head->next = NULL;
    heap_initialized = true;

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("[OK] Kernel heap initialized (64 KiB)\n");
}

/*
 * kmalloc - Allocates a contiguous block of memory from the kernel heap.
 *
 * Algorithm: First-Fit Free-List Search.
 *
 * Parameters:
 *   size - Requested allocation size in bytes.
 *
 * Returns:
 *   8-byte aligned pointer to payload on success, or NULL on failure.
 */
void *kmalloc(size_t size) {
    if (!heap_initialized || size == 0) {
        return NULL;
    }

    size_t aligned_size = heap_align_size(size);
    if (aligned_size == 0) {
        return NULL; /* Arithmetic overflow */
    }

    struct heap_block *curr = heap_head;
    while (curr != NULL) {
        if (curr->free && curr->size >= aligned_size) {
            /*
             * Check if remaining space is large enough to warrant splitting.
             * Requires room for a new block header + minimum aligned payload (8 bytes).
             */
            if (curr->size >= aligned_size + sizeof(struct heap_block) + HEAP_MIN_PAYLOAD) {
                struct heap_block *new_block = (struct heap_block *)((uint8_t *)(curr + 1) + aligned_size);
                new_block->size = curr->size - aligned_size - sizeof(struct heap_block);
                new_block->free = true;
                new_block->next = curr->next;

                curr->size = aligned_size;
                curr->next = new_block;
            }

            curr->free = false;
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }

    return NULL; /* Out of suitable memory blocks */
}

/*
 * heap_coalesce - Merges physically adjacent free blocks in the linked list.
 *
 * Traverses from heap_head and combines contiguous free blocks into a single
 * larger free block. Correctly coalesces:
 *   - current + next
 *   - previous + current
 *   - previous + current + next
 */
static void heap_coalesce(void) {
    struct heap_block *curr = heap_head;
    while (curr != NULL && curr->next != NULL) {
        if (curr->free && curr->next->free) {
            /* Verify physical adjacency in virtual address space */
            if ((uint8_t *)(curr + 1) + curr->size == (uint8_t *)curr->next) {
                curr->size += sizeof(struct heap_block) + curr->next->size;
                curr->next = curr->next->next;
                continue; /* Re-check merged block with subsequent neighbor */
            }
        }
        curr = curr->next;
    }
}

/*
 * kfree - Releases a previously allocated memory block back to the heap.
 *
 * Validates pointer boundaries, alignment, and existence in the active block
 * list to prevent heap corruption, invalid dereferencing, or double-frees.
 * Automatically coalesces adjacent free blocks upon completion.
 *
 * Parameters:
 *   ptr - Pointer returned by a prior kmalloc() call, or NULL.
 */
void kfree(void *ptr) {
    if (!heap_initialized || ptr == NULL) {
        return;
    }

    uintptr_t uptr = (uintptr_t)ptr;

    /* Validate pointer falls within heap bounds */
    if (uptr < (uintptr_t)HEAP_START + sizeof(struct heap_block) ||
        uptr >= (uintptr_t)HEAP_START + HEAP_SIZE) {
        return; /* Out of heap bounds */
    }

    /* Validate 8-byte alignment */
    if ((uptr & (HEAP_ALIGNMENT - 1)) != 0) {
        return; /* Misaligned pointer */
    }

    /* Recover block header and locate in block list */
    struct heap_block *target = (struct heap_block *)ptr - 1;
    struct heap_block *curr = heap_head;
    bool found = false;

    while (curr != NULL) {
        if (curr == target) {
            found = true;
            break;
        }
        curr = curr->next;
    }

    /* Guard against invalid headers or double-frees */
    if (!found || curr->free) {
        return;
    }

    /* Mark block as free and coalesce adjacent free blocks */
    target->free = true;
    heap_coalesce();
}

/*
 * heap_get_stats - Gathers a snapshot of current heap usage and fragmentation.
 *
 * Parameters:
 *   stats - Pointer to heap_stats_t structure to populate.
 */
void heap_get_stats(heap_stats_t *stats) {
    if (!stats) return;

    stats->start_addr   = HEAP_START;
    stats->total_size   = HEAP_SIZE;
    stats->used_bytes   = 0;
    stats->free_bytes   = 0;
    stats->total_blocks = 0;
    stats->free_blocks  = 0;
    stats->used_blocks  = 0;
    stats->largest_free = 0;

    if (!heap_initialized) {
        return;
    }

    struct heap_block *curr = heap_head;
    while (curr != NULL) {
        stats->total_blocks++;
        if (curr->free) {
            stats->free_blocks++;
            stats->free_bytes += curr->size;
            if (curr->size > stats->largest_free) {
                stats->largest_free = curr->size;
            }
        } else {
            stats->used_blocks++;
            stats->used_bytes += curr->size;
        }
        curr = curr->next;
    }
}

/*
 * heap_run_test - Comprehensive automated in-kernel test suite for Stage 6.
 *
 * Verifies:
 *   1. kmalloc() returns non-NULL for valid requests.
 *   2. Returned pointers are 8-byte aligned.
 *   3. Distinct allocations produce non-overlapping memory regions.
 *   4. Memory write and readback integrity across blocks.
 *   5. kfree() releases memory.
 *   6. Freed blocks can be reused.
 *   7. Block splitting creates separate allocated and free blocks.
 *   8. Coalescing merges adjacent free blocks completely.
 *   9. kmalloc(0) safely returns NULL.
 *  10. Oversized allocation safely returns NULL.
 *  11. kfree(NULL) and out-of-bounds pointers are handled safely.
 *  12. Total heap bytes and metadata remain 100% internally consistent.
 *
 * Returns:
 *   0 on success, negative error code on failure.
 */
/*
 * print_test_step - Helper to print consistent test results.
 */
static void print_test_step(const char *name, bool ok) {
    vga_puts("  ");
    vga_puts(name);
    vga_puts(": ");
    vga_puts(ok ? "OK\n" : "FAIL\n");
}

/*
 * heap_run_test - Comprehensive automated in-kernel test suite for Stage 6.
 *
 * Verifies:
 *   1. kmalloc() returns non-NULL for valid requests.
 *   2. Returned pointers are 8-byte aligned.
 *   3. Distinct allocations produce non-overlapping memory regions.
 *   4. Memory write and readback integrity across blocks.
 *   5. kfree() releases memory.
 *   6. Freed blocks can be reused.
 *   7. Block splitting creates separate allocated and free blocks.
 *   8. Coalescing merges adjacent free blocks completely.
 *   9. kmalloc(0) safely returns NULL.
 *  10. Oversized allocation safely returns NULL.
 *  11. kfree(NULL) and out-of-bounds pointers are handled safely.
 *  12. Total heap bytes and metadata remain 100% internally consistent.
 *
 * Returns:
 *   0 on success, negative error code on failure.
 */
int heap_run_test(void) {
    vga_puts("Heap Test:\n");

    /* 1. Basic allocation */
    void *p1 = kmalloc(128);
    print_test_step("kmalloc basic allocation", p1 != NULL);
    if (!p1) {
        return -1;
    }

    /* 2. Alignment */
    void *p_odd = kmalloc(13);
    bool align_ok = (p_odd != NULL) &&
                    (((uintptr_t)p1 & 7) == 0) &&
                    (((uintptr_t)p_odd & 7) == 0);
    print_test_step("alignment", align_ok);
    if (!align_ok) {
        kfree(p1);
        kfree(p_odd);
        return -2;
    }

    /* 3. Distinct allocations */
    void *p2 = kmalloc(256);
    uintptr_t u1 = (uintptr_t)p1;
    uintptr_t u2 = (uintptr_t)p2;
    bool distinct_ok = (p2 != NULL) && (p1 != p2) && (p1 != p_odd) && (p2 != p_odd);
    if (u1 < u2) {
        if (u1 + 128 > u2) distinct_ok = false;
    } else {
        if (u2 + 256 > u1) distinct_ok = false;
    }
    print_test_step("distinct allocations", distinct_ok);
    if (!distinct_ok) {
        kfree(p1);
        kfree(p_odd);
        kfree(p2);
        return -3;
    }

    /* 4. Memory write/read */
    uint8_t *b1 = (uint8_t *)p1;
    uint8_t *b2 = (uint8_t *)p2;
    for (int i = 0; i < 128; i++) {
        b1[i] = (uint8_t)(i ^ 0xAA);
    }
    for (int i = 0; i < 256; i++) {
        b2[i] = (uint8_t)(i ^ 0x55);
    }
    bool mem_ok = true;
    for (int i = 0; i < 128; i++) {
        if (b1[i] != (uint8_t)(i ^ 0xAA)) {
            mem_ok = false;
            break;
        }
    }
    for (int i = 0; i < 256; i++) {
        if (b2[i] != (uint8_t)(i ^ 0x55)) {
            mem_ok = false;
            break;
        }
    }
    print_test_step("memory write/read", mem_ok);
    if (!mem_ok) {
        kfree(p1);
        kfree(p_odd);
        kfree(p2);
        return -4;
    }

    /* 5. Free */
    kfree(p_odd);
    kfree(p2);
    bool free_ok = true;
    for (int i = 0; i < 128; i++) {
        if (b1[i] != (uint8_t)(i ^ 0xAA)) {
            free_ok = false;
            break;
        }
    }
    print_test_step("free", free_ok);
    if (!free_ok) {
        kfree(p1);
        return -5;
    }

    /* 6. Block reuse */
    void *p3 = kmalloc(128);
    bool reuse_ok = (p3 != NULL);
    print_test_step("block reuse", reuse_ok);
    kfree(p3);
    kfree(p1);
    if (!reuse_ok) {
        return -6;
    }

    /* 7. Block splitting */
    void *a = kmalloc(64);
    void *b = kmalloc(64);
    heap_stats_t s;
    heap_get_stats(&s);
    bool split_ok = (s.total_blocks >= 3);
    print_test_step("block splitting", split_ok);
    if (!split_ok) {
        kfree(a);
        kfree(b);
        return -7;
    }

    /* 8. Block coalescing */
    kfree(a);
    kfree(b);
    heap_get_stats(&s);
    bool coalesce_ok = (s.free_blocks == 1 && s.used_blocks == 0);
    print_test_step("block coalescing", coalesce_ok);
    if (!coalesce_ok) {
        return -8;
    }

    /* 9. Zero-size allocation */
    void *pz = kmalloc(0);
    print_test_step("zero-size allocation", pz == NULL);
    if (pz != NULL) {
        return -9;
    }

    /* 10. Oversized allocation */
    void *po = kmalloc(HEAP_SIZE * 2);
    print_test_step("oversized allocation", po == NULL);
    if (po != NULL) {
        return -10;
    }

    /* 11. Null / invalid pointer safety */
    kfree(NULL);
    kfree((void *)0x12345ULL);
    kfree((void *)(HEAP_START + 5));
    heap_get_stats(&s);
    if (s.free_blocks != 1 || s.used_blocks != 0) {
        return -11;
    }

    /* 12. Statistics consistency */
    size_t expected_free = HEAP_SIZE - sizeof(struct heap_block);
    if (s.free_bytes != expected_free || s.used_bytes != 0 || s.total_blocks != 1 ||
        (s.used_bytes + s.free_bytes + (s.total_blocks * sizeof(struct heap_block))) != HEAP_SIZE) {
        return -12;
    }

    vga_puts("Heap test passed!\n");
    return 0;
}
