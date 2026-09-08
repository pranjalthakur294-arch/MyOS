#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

/* Standard x86-64 physical frame size: 4 KiB */
#define PAGE_SIZE 4096ULL

/*
 * Maximum physical address supported by initial identity mapping: 1 GiB.
 * 1 GiB / 4 KiB = 262,144 frames.
 * 262,144 frames / 8 bits per byte = 32,768 bytes (32 KiB) bitmap in BSS.
 */
#define PMM_MAX_PHYSICAL_MEMORY (1024ULL * 1024ULL * 1024ULL)
#define PMM_BITMAP_SIZE         (PMM_MAX_PHYSICAL_MEMORY / PAGE_SIZE / 8)

/* Public PMM API */
void pmm_init(uint64_t multiboot_info_addr);

uint64_t pmm_alloc_frame(void);
void pmm_free_frame(uint64_t physical_address);

uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_free_memory(void);
uint64_t pmm_get_used_memory(void);

uint64_t pmm_get_total_frames(void);
uint64_t pmm_get_free_frames(void);
uint64_t pmm_get_used_frames(void);

#endif /* PMM_H */
