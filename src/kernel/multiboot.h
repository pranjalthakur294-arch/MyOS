#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

/*
 * Multiboot Specification Version 0.6.96 Definitions
 */

/* Magic number passed in EAX by a Multiboot-compliant bootloader */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Flags in the Multiboot Information structure */
#define MULTIBOOT_INFO_MEMORY    (1 << 0)  /* mem_lower and mem_upper are valid */
#define MULTIBOOT_INFO_BOOTDEV   (1 << 1)  /* boot_device is valid */
#define MULTIBOOT_INFO_CMDLINE   (1 << 2)  /* cmdline is valid */
#define MULTIBOOT_INFO_MODS      (1 << 3)  /* mods_count and mods_addr are valid */
#define MULTIBOOT_INFO_MEM_MAP   (1 << 6)  /* mmap_length and mmap_addr are valid */

/* Memory map entry type values */
#define MULTIBOOT_MEMORY_AVAILABLE        1  /* Usable RAM available for operating system */
#define MULTIBOOT_MEMORY_RESERVED         2  /* Reserved / non-usable hardware memory */
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3  /* ACPI reclaimable memory */
#define MULTIBOOT_MEMORY_NVS              4  /* ACPI non-volatile storage */
#define MULTIBOOT_MEMORY_BADRAM           5  /* Defective RAM */

/*
 * Multiboot Memory Map Entry structure
 *
 * Notice: 'size' is the size of the structure elements following this field
 * (i.e. sizeof(entry) - 4). To advance to the next entry:
 *   next = (struct multiboot_mmap_entry *)((uint8_t *)curr + curr->size + 4);
 */
struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

/*
 * Multiboot Information structure passed to the kernel by the bootloader
 */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
} __attribute__((packed));

#endif /* MULTIBOOT_H */
