#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Kernel Heap Virtual Memory Parameters
 *
 * Location:
 *   HEAP_START: 0x50000000 (1 GiB + 256 MiB, inside PDPT[1], PD[128])
 *   HEAP_SIZE:  64 KiB (65536 bytes = 16 x 4 KiB pages)
 *
 * This region is strictly isolated from:
 *   - 0..1 GiB boot identity mapping (PDPT[0])
 *   - VMM test page at 0x40000000 (PDPT[1], PD[0])
 *   - Kernel image (0x00100000..0x00117000) and VGA memory (0x000B8000)
 */
#define HEAP_START          0x50000000ULL
#define HEAP_SIZE           (64ULL * 1024ULL)
#define HEAP_ALIGNMENT      8ULL
#define HEAP_MIN_PAYLOAD    8ULL

/*
 * Header preceding every allocated or free block in the heap.
 * Total size: 24 bytes (strictly 8-byte aligned on x86-64).
 */
struct heap_block {
    size_t size;              /* Payload size in bytes (excluding this header) */
    bool free;                /* true if block is free, false if allocated */
    struct heap_block *next;  /* Pointer to next block in heap order */
};

/*
 * Statistics snapshot structure for heap inspection and monitoring.
 */
typedef struct {
    uint64_t start_addr;      /* Base virtual address of the heap */
    size_t total_size;        /* Total heap capacity in bytes */
    size_t used_bytes;        /* Sum of payload bytes in allocated blocks */
    size_t free_bytes;        /* Sum of payload bytes in free blocks */
    size_t total_blocks;      /* Total number of blocks (free + used) */
    size_t free_blocks;       /* Number of currently free blocks */
    size_t used_blocks;       /* Number of currently allocated blocks */
    size_t largest_free;      /* Largest single free payload available */
} heap_stats_t;

/*
 * Public Kernel Heap API
 */
void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void heap_get_stats(heap_stats_t *stats);
int heap_run_test(void);

#endif /* HEAP_H */
