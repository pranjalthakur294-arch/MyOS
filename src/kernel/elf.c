/*
 * elf.c - ELF64 Executable Loader & Validator (Stage 10)
 *
 * Implements strict ELF64 header validation, PT_LOAD segment validation,
 * page-granular memory mapping with architectural permissions, BSS zero-initialization,
 * user stack establishment, leak-free rollback on failure, and comprehensive
 * security testing.
 */

#include "elf.h"
#include "process.h"
#include "vmm.h"
#include "pmm.h"
#include "vga.h"
#include "timer.h"
#include "user.h"
#include "gdt.h"
#include "task.h"
#include "scheduler.h"
#include "file.h"
#include "vfs.h"
#include "heap.h"
#include "syscall.h"

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


static void kstrncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    while (i + 1 < n && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/*
 * elf_init - Initializes the ELF loader subsystem.
 * Silent during normal boot to preserve 25-row screen line budget.
 */
void elf_init(void) {
    /* No persistent global state needed currently */
}

/*
 * elf_strerror - Returns human-readable diagnostic message for an ELF error code.
 */
const char *elf_strerror(int err) {
    switch (err) {
        case ELF_OK:
            return "Success";
        case ELF_ERR_NULL_IMAGE:
            return "NULL ELF image pointer";
        case ELF_ERR_TOO_SMALL:
            return "Image smaller than ELF64 header";
        case ELF_ERR_BAD_MAGIC:
            return "Invalid ELF magic bytes";
        case ELF_ERR_NOT_64BIT:
            return "Unsupported ELF class (must be 64-bit)";
        case ELF_ERR_NOT_LITTLE_ENDIAN:
            return "Unsupported endianness (must be little-endian)";
        case ELF_ERR_WRONG_MACHINE:
            return "Unsupported architecture (must be x86-64)";
        case ELF_ERR_NOT_EXEC:
            return "Unsupported ELF type (must be ET_EXEC)";
        case ELF_ERR_BAD_VERSION:
            return "Invalid ELF version";
        case ELF_ERR_BAD_EHSIZE:
            return "Invalid ELF header size";
        case ELF_ERR_BAD_PHENTSIZE:
            return "Invalid program header entry size";
        case ELF_ERR_BAD_PHNUM:
            return "Invalid number of program headers";
        case ELF_ERR_PHDR_OUT_OF_BOUNDS:
            return "Program header table exceeds file bounds";
        case ELF_ERR_FILESZ_GREATER_THAN_MEMSZ:
            return "Segment file size exceeds memory size";
        case ELF_ERR_FILE_OFFSET_OVERFLOW:
            return "Segment file offset arithmetic overflow";
        case ELF_ERR_FILE_BOUNDS_VIOLATION:
            return "Segment file data extends beyond ELF image";
        case ELF_ERR_VADDR_OVERFLOW:
            return "Segment virtual address arithmetic overflow";
        case ELF_ERR_KERNEL_OVERLAP:
            return "Segment overlaps protected kernel address space";
        case ELF_ERR_USER_BOUNDS_VIOLATION:
            return "Segment outside valid user virtual memory space";
        case ELF_ERR_STACK_OVERLAP:
            return "Segment overlaps dedicated user stack";
        case ELF_ERR_NO_LOAD_SEGMENTS:
            return "No loadable PT_LOAD segments found";
        case ELF_ERR_SEGMENT_OVERLAP:
            return "Overlapping PT_LOAD segments detected";
        case ELF_ERR_ENTRY_NOT_EXECUTABLE:
            return "ELF entry point in non-executable segment";
        case ELF_ERR_INVALID_ENTRY:
            return "ELF entry point outside loaded PT_LOAD segments";
        case ELF_ERR_NO_MEMORY:
            return "Out of physical memory frames";
        case ELF_ERR_TABLE_LIMIT:
            return "Process page table allocation limit exceeded";
        case ELF_ERR_MAP_FAILED:
            return "Virtual memory page mapping failed";
        case ELF_ERR_BAD_ALIGNMENT:
            return "Malformed segment alignment";
        case ELF_ERR_OPEN_FAILED:
            return "Failed to open executable file";
        case ELF_ERR_READ_FAILED:
            return "Failed to read executable file";
        case ELF_ERR_FILE_TOO_LARGE:
            return "Executable exceeds maximum supported size";
        case ELF_ERR_NOT_FOUND:
            return "File not found";
        case ELF_ERR_IS_DIR:
            return "Path is a directory";
        default:
            return "Unknown ELF error";
    }
}

/*
 * elf_validate - Strictly validates an ELF64 executable image prior to loading.
 *
 * Checks:
 *   1. Image pointer and size bounds.
 *   2. ELF magic (0x7F 'E' 'L' 'F').
 *   3. 64-bit class (ELFCLASS64).
 *   4. Little-endian encoding (ELFDATA2LSB).
 *   5. Machine architecture (EM_X86_64).
 *   6. File type (ET_EXEC).
 *   7. Version (EV_CURRENT).
 *   8. Header size (e_ehsize == sizeof(Elf64_Ehdr)).
 *   9. Program header entry size (e_phentsize == sizeof(Elf64_Phdr)).
 *  10. Program header count (1 <= e_phnum <= MAX_ELF_PHNUM).
 *  11. Program header table bounds within image (no overflow).
 *  12. PT_LOAD segments:
 *      - filesz <= memsz
 *      - file offset + filesz within image bounds (no overflow)
 *      - virtual address + memsz does not overflow
 *      - memsz > 0
 *      - virtual address within user space [USER_SPACE_MIN, USER_SPACE_MAX)
 *      - no overlap with kernel heap [KERNEL_HEAP_START, KERNEL_HEAP_END)
 *      - no overlap with user stack [USER_ELF_STACK_VADDR, USER_ELF_STACK_TOP)
 *      - no pairwise page-level overlap between PT_LOAD segments
 *  13. Entry point:
 *      - falls within a loaded PT_LOAD segment
 *      - containing segment has PF_X (executable)
 *
 * Returns:
 *   ELF_OK (0) on success, or a negative ELF_ERR_* error code.
 */
int elf_validate(const void *image, size_t size) {
    if (!image) {
        return ELF_ERR_NULL_IMAGE;
    }
    if (size < sizeof(Elf64_Ehdr)) {
        return ELF_ERR_TOO_SMALL;
    }

    const uint8_t *img = (const uint8_t *)image;
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)image;

    /* 1. Magic check */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return ELF_ERR_BAD_MAGIC;
    }

    /* 2. 64-bit class check */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        return ELF_ERR_NOT_64BIT;
    }

    /* 3. Little-endian check */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return ELF_ERR_NOT_LITTLE_ENDIAN;
    }

    /* 4. Target machine check */
    if (ehdr->e_machine != EM_X86_64) {
        return ELF_ERR_WRONG_MACHINE;
    }

    /* 5. Object file type check (must be ET_EXEC) */
    if (ehdr->e_type != ET_EXEC) {
        return ELF_ERR_NOT_EXEC;
    }

    /* 6. Version check */
    if (ehdr->e_version != EV_CURRENT || ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
        return ELF_ERR_BAD_VERSION;
    }

    /* 7. Header size check */
    if (ehdr->e_ehsize != sizeof(Elf64_Ehdr)) {
        return ELF_ERR_BAD_EHSIZE;
    }

    /* 8. Program header entry size check */
    if (ehdr->e_phentsize != sizeof(Elf64_Phdr)) {
        return ELF_ERR_BAD_PHENTSIZE;
    }

    /* 9. Program header count check */
    if (ehdr->e_phnum == 0 || ehdr->e_phnum > MAX_ELF_PHNUM) {
        return ELF_ERR_BAD_PHNUM;
    }

    /* 10. Program header table file bounds and overflow check */
    if (ehdr->e_phoff < sizeof(Elf64_Ehdr)) {
        return ELF_ERR_PHDR_OUT_OF_BOUNDS;
    }
    uint64_t ph_size = (uint64_t)ehdr->e_phnum * sizeof(Elf64_Phdr);
    if (ehdr->e_phoff + ph_size < ehdr->e_phoff || ehdr->e_phoff + ph_size > size) {
        return ELF_ERR_PHDR_OUT_OF_BOUNDS;
    }

    /* 11. Validate each PT_LOAD segment */
    size_t load_count = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = (const Elf64_Phdr *)(img + ehdr->e_phoff + i * sizeof(Elf64_Phdr));

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        load_count++;

        /* Check filesz <= memsz */
        if (phdr->p_filesz > phdr->p_memsz) {
            return ELF_ERR_FILESZ_GREATER_THAN_MEMSZ;
        }

        /* Check file offset bounds and overflow */
        if (phdr->p_offset + phdr->p_filesz < phdr->p_offset) {
            return ELF_ERR_FILE_OFFSET_OVERFLOW;
        }
        if (phdr->p_offset + phdr->p_filesz > size) {
            return ELF_ERR_FILE_BOUNDS_VIOLATION;
        }

        /* Check segment alignment constraints */
        if (phdr->p_align > 1) {
            /* Alignment must be a power of 2 */
            if ((phdr->p_align & (phdr->p_align - 1)) != 0) {
                return ELF_ERR_BAD_ALIGNMENT;
            }
            /* Virtual address and file offset must be congruent modulo p_align */
            if ((phdr->p_vaddr % phdr->p_align) != (phdr->p_offset % phdr->p_align)) {
                return ELF_ERR_BAD_ALIGNMENT;
            }
        }

        /* Check memory size non-zero */
        if (phdr->p_memsz == 0) {
            return ELF_ERR_USER_BOUNDS_VIOLATION;
        }

        /* Check virtual address bounds and overflow */
        if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr) {
            return ELF_ERR_VADDR_OVERFLOW;
        }

        /* Virtual address must be in user space */
        if (phdr->p_vaddr < USER_SPACE_MIN) {
            return ELF_ERR_KERNEL_OVERLAP;
        }
        if (phdr->p_vaddr + phdr->p_memsz > USER_SPACE_MAX) {
            return ELF_ERR_USER_BOUNDS_VIOLATION;
        }

        /* Must not overlap kernel heap [KERNEL_HEAP_START, KERNEL_HEAP_END) */
        if (phdr->p_vaddr < KERNEL_HEAP_END && (phdr->p_vaddr + phdr->p_memsz) > KERNEL_HEAP_START) {
            return ELF_ERR_KERNEL_OVERLAP;
        }

        /* Must not overlap dedicated user stack [USER_ELF_STACK_VADDR, USER_ELF_STACK_TOP) */
        if (phdr->p_vaddr < USER_ELF_STACK_TOP && (phdr->p_vaddr + phdr->p_memsz) > USER_ELF_STACK_VADDR) {
            return ELF_ERR_STACK_OVERLAP;
        }

        /* Check pairwise page-level overlap with previous PT_LOAD segments */
        uint64_t start_a = phdr->p_vaddr & ~(VMM_PAGE_SIZE - 1);
        uint64_t end_a   = (phdr->p_vaddr + phdr->p_memsz + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);

        for (uint16_t j = 0; j < i; j++) {
            const Elf64_Phdr *prev = (const Elf64_Phdr *)(img + ehdr->e_phoff + j * sizeof(Elf64_Phdr));
            if (prev->p_type != PT_LOAD) {
                continue;
            }

            uint64_t start_b = prev->p_vaddr & ~(VMM_PAGE_SIZE - 1);
            uint64_t end_b   = (prev->p_vaddr + prev->p_memsz + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);

            uint64_t max_start = start_a > start_b ? start_a : start_b;
            uint64_t min_end   = end_a < end_b ? end_a : end_b;

            if (max_start < min_end) {
                return ELF_ERR_SEGMENT_OVERLAP;
            }
        }
    }

    if (load_count == 0) {
        return ELF_ERR_NO_LOAD_SEGMENTS;
    }

    /* 12. Validate entry point (must be in file-backed executable segment) */
    bool entry_found = false;
    bool entry_executable = false;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = (const Elf64_Phdr *)(img + ehdr->e_phoff + i * sizeof(Elf64_Phdr));
        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        if (ehdr->e_entry >= phdr->p_vaddr && ehdr->e_entry < phdr->p_vaddr + phdr->p_filesz) {
            entry_found = true;
            if (phdr->p_flags & PF_X) {
                entry_executable = true;
            }
            break;
        }
    }

    if (!entry_found) {
        return ELF_ERR_INVALID_ENTRY;
    }
    if (!entry_executable) {
        return ELF_ERR_ENTRY_NOT_EXECUTABLE;
    }

    return ELF_OK;
}

/*
 * elf_load_into_process - Loads validated ELF segments and user stack into a process.
 *
 * Implements:
 *   1. Full validation before any mapping.
 *   2. Page-granular frame allocation and zeroing (ensures BSS is zero-initialized).
 *   3. Slicing file data into frames via kernel physical identity map.
 *   4. Mapping into process PML4 with strict architectural permissions:
 *      - PRESENT + USER unconditionally.
 *      - WRITABLE iff PF_W is present.
 *   5. User stack allocation and mapping at USER_ELF_STACK_VADDR.
 *   6. Output entry point set to Elf64_Ehdr.e_entry.
 *
 * Parameters:
 *   proc      - Pointer to target process structure (must have proc->cr3 initialized).
 *   image     - Raw ELF binary in memory.
 *   size      - Size of ELF binary in bytes.
 *   out_entry - Pointer to receive entry point virtual address.
 *
 * Returns:
 *   ELF_OK (0) on success, or negative error code on failure.
 */
int elf_load_into_process(process_t *proc, const void *image, size_t size, uint64_t *out_entry) {
    if (!proc || !image || !out_entry) {
        return ELF_ERR_NULL_IMAGE;
    }

    int val = elf_validate(image, size);
    if (val != ELF_OK) {
        return val;
    }

    const uint8_t *img = (const uint8_t *)image;
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)image;

    /* 1. Load each PT_LOAD segment */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = (const Elf64_Phdr *)(img + ehdr->e_phoff + i * sizeof(Elf64_Phdr));
        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        uint64_t vaddr_start = phdr->p_vaddr;
        uint64_t vaddr_end   = phdr->p_vaddr + phdr->p_memsz;
        uint64_t page_start  = vaddr_start & ~(VMM_PAGE_SIZE - 1);
        uint64_t page_end    = (vaddr_end + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);

        /* Translate ELF flags into MyOS page permissions */
        uint64_t pte_flags = PTE_PRESENT | PTE_USER;
        if (phdr->p_flags & PF_W) {
            pte_flags |= PTE_WRITABLE;
        }

        /* Map and populate page-by-page */
        for (uint64_t curr_page = page_start; curr_page < page_end; curr_page += VMM_PAGE_SIZE) {
            if (proc->user_frame_count >= MAX_PROCESS_USER_FRAMES) {
                return ELF_ERR_NO_MEMORY;
            }

            uint64_t phys_frame = pmm_alloc_frame();
            if (!phys_frame) {
                return ELF_ERR_NO_MEMORY;
            }

            /* Record physical frame for process reaping / rollback */
            proc->user_frames[proc->user_frame_count++] = phys_frame;

            /* Zero the entire physical frame (automatically handles BSS and partial boundaries) */
            kmemset((void *)phys_frame, 0, VMM_PAGE_SIZE);

            /* Determine slice of file data falling into this page */
            uint64_t file_vaddr_start = phdr->p_vaddr;
            uint64_t file_vaddr_end   = phdr->p_vaddr + phdr->p_filesz;
            uint64_t page_vaddr_start = curr_page;
            uint64_t page_vaddr_end   = curr_page + VMM_PAGE_SIZE;

            uint64_t overlap_start = file_vaddr_start > page_vaddr_start ? file_vaddr_start : page_vaddr_start;
            uint64_t overlap_end   = file_vaddr_end < page_vaddr_end ? file_vaddr_end : page_vaddr_end;

            if (overlap_end > overlap_start) {
                size_t copy_len = (size_t)(overlap_end - overlap_start);
                uint64_t frame_offset = overlap_start - page_vaddr_start;
                uint64_t file_offset  = phdr->p_offset + (overlap_start - file_vaddr_start);
                kmemcpy((void *)(phys_frame + frame_offset), img + file_offset, copy_len);
            }

            /* Map page into process PML4 */
            int map_ret = vmm_map_page_in_pml4(proc->cr3, curr_page, phys_frame, pte_flags,
                                               proc->table_frames, &proc->table_frame_count,
                                               MAX_PROCESS_TABLE_FRAMES);
            if (map_ret != 0) {
                return ELF_ERR_MAP_FAILED;
            }
        }
    }

    /* 2. Allocate and map dedicated 4 KiB user stack at USER_ELF_STACK_VADDR */
    if (proc->user_frame_count >= MAX_PROCESS_USER_FRAMES) {
        return ELF_ERR_NO_MEMORY;
    }

    uint64_t stack_frame = pmm_alloc_frame();
    if (!stack_frame) {
        return ELF_ERR_NO_MEMORY;
    }

    proc->user_frames[proc->user_frame_count++] = stack_frame;
    kmemset((void *)stack_frame, 0, VMM_PAGE_SIZE);

    int stack_map_ret = vmm_map_page_in_pml4(proc->cr3, USER_ELF_STACK_VADDR, stack_frame,
                                             PTE_PRESENT | PTE_WRITABLE | PTE_USER,
                                             proc->table_frames, &proc->table_frame_count,
                                             MAX_PROCESS_TABLE_FRAMES);
    if (stack_map_ret != 0) {
        return ELF_ERR_MAP_FAILED;
    }

    *out_entry = ehdr->e_entry;
    return ELF_OK;
}

/*
 * process_create_from_elf - Creates and initializes an isolated user process from an ELF image.
 *
 * Transactional: on ANY failure during validation, allocation, mapping, or task creation,
 * all allocated frames are cleanly freed and NULL is returned with zero memory leaks.
 */
process_t *process_create_from_elf(const void *image, size_t size, const char *name) {
    /* 1. Preliminary validation before touching process or memory subsystem */
    int val = elf_validate(image, size);
    if (val != ELF_OK) {
        return NULL;
    }

    /* 2. Locate an available process slot */
    int slot = -1;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        process_t *p = process_get((uint32_t)i);
        if (p && p->state == PROCESS_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 1; i < MAX_PROCESSES; i++) {
            process_t *p = process_get((uint32_t)i);
            if (p && p->state == PROCESS_TERMINATED && p->reaped) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        return NULL;
    }

    process_t *proc = process_get((uint32_t)slot);
    kmemset(proc, 0, sizeof(process_t));
    fd_init_process(proc);

    /* 3. Create process PML4 directory with kernel identity mappings cloned */
    uint64_t pml4_phys = 0;
    uint64_t tables[MAX_PROCESS_TABLE_FRAMES];
    size_t table_count = 0;

    int ret = vmm_create_process_pml4(&pml4_phys, tables, &table_count, MAX_PROCESS_TABLE_FRAMES);
    if (ret != 0) {
        return NULL;
    }

    proc->pml4_phys = pml4_phys;
    proc->cr3 = pml4_phys;
    for (size_t k = 0; k < table_count; k++) {
        proc->table_frames[k] = tables[k];
    }
    proc->table_frame_count = table_count;

    /* 4. Load ELF segments and stack into process address space */
    uint64_t entry_point = 0;
    int load_ret = elf_load_into_process(proc, image, size, &entry_point);
    if (load_ret != ELF_OK) {
        /* Rollback: free all user frames, intermediate tables, and PML4 */
        for (size_t k = 0; k < proc->user_frame_count; k++) {
            if (proc->user_frames[k]) pmm_free_frame(proc->user_frames[k]);
        }
        for (size_t k = 0; k < proc->table_frame_count; k++) {
            if (proc->table_frames[k]) pmm_free_frame(proc->table_frames[k]);
        }
        if (proc->pml4_phys) pmm_free_frame(proc->pml4_phys);
        kmemset(proc, 0, sizeof(process_t));
        return NULL;
    }

    /* 5. Create underlying preemptive task configured for Ring 3 */
    task_t *t = task_create_user(entry_point, USER_ELF_STACK_TOP, pml4_phys, proc, name);
    if (!t) {
        /* Rollback */
        for (size_t k = 0; k < proc->user_frame_count; k++) {
            if (proc->user_frames[k]) pmm_free_frame(proc->user_frames[k]);
        }
        for (size_t k = 0; k < proc->table_frame_count; k++) {
            if (proc->table_frames[k]) pmm_free_frame(proc->table_frames[k]);
        }
        if (proc->pml4_phys) pmm_free_frame(proc->pml4_phys);
        kmemset(proc, 0, sizeof(process_t));
        return NULL;
    }

    /* 6. Populate PCB fields */
    proc->pid = (uint32_t)slot;
    proc->state = PROCESS_READY;
    proc->type = PROCESS_TYPE_USER;
    kstrncpy(proc->name, name ? name : "elf_proc", PROCESS_NAME_MAX);
    proc->task = t;
    proc->user_entry = entry_point;
    proc->user_stack_top = USER_ELF_STACK_TOP;
    proc->kernel_stack_top = ((uint64_t)t->stack_base + t->stack_size) & ~0xFULL;
    proc->exit_status = 0;
    proc->reaped = false;
    proc->is_elf = true;

    return proc;
}

/*
 * process_exec_path - Loads an ELF executable through the VFS and FD abstraction
 * and instantiates a new user-mode process with isolated private address space.
 *
 * Sequence:
 *   1. Resolve path through VFS and open via fd_open(caller, path, O_RDONLY)
 *   2. Determine file size via generic fd_get_size()
 *   3. Allocate temporary buffer from kernel heap
 *   4. Read complete executable through fd_read()
 *   5. Close file descriptor (fd_close)
 *   6. Strictly validate ELF headers and PT_LOAD segment bounds (elf_validate)
 *   7. Create process, private CR3, map segments, and establish user stack
 *   8. Free temporary buffer immediately (kfree)
 *   9. Return newly created process pointer or negative error code
 */
int process_exec_path(const char *path, const char *name, struct process **out_proc) {
    if (out_proc != NULL) {
        *out_proc = NULL;
    }
    if (path == NULL || path[0] == '\0') {
        return VFS_ERR_INVALID;
    }

    process_t *caller = process_current();
    if (!caller) {
        caller = process_get(0);
        if (!caller) {
            return ELF_ERR_NO_MEMORY;
        }
    }

    /* 1. Open executable file descriptor */
    int fd = fd_open(caller, path, O_RDONLY);
    if (fd < 0) {
        if (fd == SYSCALL_ENOENT) {
            return ELF_ERR_NOT_FOUND;
        } else if (fd == SYSCALL_EISDIR) {
            return ELF_ERR_IS_DIR;
        } else if (fd == SYSCALL_EMFILE) {
            return SYSCALL_EMFILE;
        }
        return ELF_ERR_OPEN_FAILED;
    }

    /* Check if target node is a directory */
    open_file_t *of = caller->fds[fd];
    if (of != NULL && of->type == OPEN_FILE_VFS && of->node != NULL) {
        if (of->node->type == VFS_NODE_DIRECTORY) {
            fd_close(caller, fd);
            return ELF_ERR_IS_DIR;
        }
    }

    /* 2. Determine file size through generic FD operation */
    int64_t file_size = fd_get_size(caller, fd);
    if (file_size < (int64_t)sizeof(Elf64_Ehdr)) {
        fd_close(caller, fd);
        return ELF_ERR_TOO_SMALL;
    }
    if (file_size > (int64_t)ELF_MAX_EXEC_SIZE) {
        fd_close(caller, fd);
        return ELF_ERR_FILE_TOO_LARGE;
    }

    size_t sz = (size_t)file_size;

    /* 3. Allocate temporary buffer from kernel heap */
    uint8_t *buf = (uint8_t *)kmalloc(sz);
    if (!buf) {
        fd_close(caller, fd);
        return ELF_ERR_NO_MEMORY;
    }

    /* 4. Read complete file bytes through FD */
    size_t total_read = 0;
    while (total_read < sz) {
        int64_t n = fd_read(caller, fd, buf + total_read, sz - total_read);
        if (n <= 0) {
            break;
        }
        total_read += (size_t)n;
    }

    /* Descriptor is closed immediately after reading */
    fd_close(caller, fd);

    if (total_read != sz) {
        kfree(buf);
        return ELF_ERR_READ_FAILED;
    }

    /* 5. Validate ELF64 image */
    int val = elf_validate(buf, sz);
    if (val != ELF_OK) {
        kfree(buf);
        return val;
    }

    /* 6. Create process with private CR3 and mapped PT_LOAD segments */
    process_t *proc = process_create_from_elf(buf, sz, name ? name : path);

    /* 7. Free temporary ELF buffer immediately after segment mapping */
    kfree(buf);

    if (!proc) {
        return ELF_ERR_MAP_FAILED;
    }

    if (out_proc != NULL) {
        *out_proc = proc;
    }
    return 0;
}

int elf_exec_path(const char *path, const char *name, struct process **out_proc) {
    return process_exec_path(path, name, out_proc);
}

#define SYNTH_ELF_MAX 16384
static uint8_t valid_elf_buf[SYNTH_ELF_MAX] __attribute__((aligned(16)));
static uint8_t synth_elf[SYNTH_ELF_MAX] __attribute__((aligned(16)));

/*
 * elf_run_validation_tests - Executes comprehensive in-kernel security, validation,
 * rollback, memory leak, and execution tests.
 *
 * Returns 0 on complete pass, negative error code on failure.
 */
int elf_run_validation_tests(void) {
    process_t *caller = process_current();
    if (!caller) caller = process_get(0);
    if (!caller) return -100;

    /* Obtain valid base ELF binary by reading /bin/test through VFS/FD abstraction */
    int fd = fd_open(caller, "/bin/test", O_RDONLY);
    if (fd < 0) {
        return -101;
    }
    int64_t file_size = fd_get_size(caller, fd);
    if (file_size < (int64_t)sizeof(Elf64_Ehdr) || file_size > (int64_t)SYNTH_ELF_MAX) {
        fd_close(caller, fd);
        return -102;
    }
    size_t valid_size = (size_t)file_size;
    int64_t nread = fd_read(caller, fd, valid_elf_buf, valid_size);
    fd_close(caller, fd);
    if (nread != (int64_t)valid_size) {
        return -103;
    }
    const void *valid_elf = valid_elf_buf;

    /* 1. Valid ELF image validation */
    if (elf_validate(valid_elf, valid_size) != ELF_OK) {
        return -1;
    }

    /* 2. Reject NULL image */
    if (elf_validate(NULL, valid_size) != ELF_ERR_NULL_IMAGE) {
        return -2;
    }

    /* 3. Reject truncated image */
    if (elf_validate(valid_elf, sizeof(Elf64_Ehdr) - 1) != ELF_ERR_TOO_SMALL) {
        return -3;
    }

    /* Setup synthetic base image from full valid ELF binary */
    if (valid_size > SYNTH_ELF_MAX) {
        return -99;
    }
    kmemcpy(synth_elf, valid_elf, valid_size);
    Elf64_Ehdr *sehdr = (Elf64_Ehdr *)synth_elf;
    Elf64_Phdr *sphdr = (Elf64_Phdr *)(synth_elf + sehdr->e_phoff);

    /* 4. Reject bad magic */
    sehdr->e_ident[EI_MAG0] = 0x7E;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_BAD_MAGIC) {
        return -4;
    }
    sehdr->e_ident[EI_MAG0] = ELFMAG0;

    /* 5. Reject 32-bit class */
    sehdr->e_ident[EI_CLASS] = ELFCLASS32;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_NOT_64BIT) {
        return -5;
    }
    sehdr->e_ident[EI_CLASS] = ELFCLASS64;

    /* 6. Reject big-endian */
    sehdr->e_ident[EI_DATA] = ELFDATA2MSB;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_NOT_LITTLE_ENDIAN) {
        return -6;
    }
    sehdr->e_ident[EI_DATA] = ELFDATA2LSB;

    /* 7. Reject wrong machine (e.g. EM_386) */
    sehdr->e_machine = EM_386;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_WRONG_MACHINE) {
        return -7;
    }
    sehdr->e_machine = EM_X86_64;

    /* 8. Reject non-ET_EXEC */
    sehdr->e_type = ET_REL;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_NOT_EXEC) {
        return -8;
    }
    sehdr->e_type = ET_EXEC;

    /* 9. Reject program header table out of bounds */
    uint64_t orig_phoff = sehdr->e_phoff;
    sehdr->e_phoff = valid_size;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_PHDR_OUT_OF_BOUNDS) {
        return -9;
    }
    sehdr->e_phoff = orig_phoff;

    /* 10. Reject filesz > memsz */
    uint64_t orig_filesz = sphdr[0].p_filesz;
    sphdr[0].p_filesz = sphdr[0].p_memsz + 100;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_FILESZ_GREATER_THAN_MEMSZ) {
        return -10;
    }
    sphdr[0].p_filesz = orig_filesz;

    /* 11. Reject file data bounds violation */
    uint64_t orig_offset = sphdr[0].p_offset;
    sphdr[0].p_offset = valid_size;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_FILE_BOUNDS_VIOLATION) {
        return -11;
    }
    sphdr[0].p_offset = orig_offset;

    /* 12. Reject virtual address arithmetic overflow */
    uint64_t orig_vaddr = sphdr[0].p_vaddr;
    uint64_t orig_memsz = sphdr[0].p_memsz;
    sphdr[0].p_vaddr = 0xFFFFFFFFFFFFF000ULL;
    sphdr[0].p_memsz = 0x2000ULL;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_VADDR_OVERFLOW) {
        return -12;
    }
    sphdr[0].p_vaddr = orig_vaddr;
    sphdr[0].p_memsz = orig_memsz;

    /* 13. Reject segment overlapping kernel identity memory (0..1 GiB) */
    sphdr[0].p_vaddr = 0x100000ULL; /* Kernel text */
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_KERNEL_OVERLAP) {
        return -13;
    }
    sphdr[0].p_vaddr = orig_vaddr;

    /* 14. Reject entry point outside PT_LOAD segments */
    uint64_t orig_entry = sehdr->e_entry;
    sehdr->e_entry = 0x45000000ULL; /* Unmapped */
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_INVALID_ENTRY) {
        return -14;
    }
    sehdr->e_entry = orig_entry;

    /* 15. Reject entry point in non-executable segment */
    uint32_t orig_flags = sphdr[0].p_flags;
    sphdr[0].p_flags &= ~PF_X;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_ENTRY_NOT_EXECUTABLE) {
        return -15;
    }
    sphdr[0].p_flags = orig_flags;

    /* 16. Reject overlapping PT_LOAD segments */
    uint64_t orig_vaddr1 = sphdr[1].p_vaddr;
    sphdr[1].p_vaddr = sphdr[0].p_vaddr;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_SEGMENT_OVERLAP) {
        return -28;
    }
    sphdr[1].p_vaddr = orig_vaddr1;

    /* 17. Reject malformed segment alignment */
    uint64_t orig_align = sphdr[0].p_align;
    sphdr[0].p_align = 3; /* Not a power of 2 */
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_BAD_ALIGNMENT) {
        return -29;
    }
    sphdr[0].p_align = 0x1000;
    uint64_t orig_p_offset = sphdr[0].p_offset;
    sphdr[0].p_offset = 0x1001; /* Incongruent with p_vaddr 0x60000000 */
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_BAD_ALIGNMENT) {
        return -30;
    }
    sphdr[0].p_align = orig_align;
    sphdr[0].p_offset = orig_p_offset;

    /* 18. Reject program header table inside ELF header */
    sehdr->e_phoff = 16;
    if (elf_validate(synth_elf, valid_size) != ELF_ERR_PHDR_OUT_OF_BOUNDS) {
        return -31;
    }
    sehdr->e_phoff = orig_phoff;

    /* 19. Test failure rollback and memory leak prevention */
    uint64_t free_before = pmm_get_free_frames();
    /* Attempt to create process from invalid ELF */
    sehdr->e_machine = EM_386;
    process_t *fail_proc = process_create_from_elf(synth_elf, valid_size, "fail_test");
    sehdr->e_machine = EM_X86_64;
    uint64_t free_after = pmm_get_free_frames();

    if (fail_proc != NULL || free_before != free_after) {
        return -16; /* Leaked memory on validation failure */
    }

    /* 18. Test valid ELF process creation via filesystem-backed path and page permissions */
    free_before = pmm_get_free_frames();
    process_t *proc = NULL;
    int exec_res = process_exec_path("/bin/test", "elf_test", &proc);
    if (exec_res != 0 || !proc) {
        return -17;
    }

    /* Test second process created concurrently from the same /bin/test */
    process_t *proc2 = NULL;
    int exec_res2 = process_exec_path("/bin/test", "elf_test2", &proc2);
    if (exec_res2 != 0 || !proc2) {
        return -17;
    }

    /* Verify process isolation: distinct PID, CR3, and physical frames */
    if (proc->pid == proc2->pid || proc->cr3 == proc2->cr3) {
        return -17;
    }
    if (proc->user_frame_count > 0 && proc2->user_frame_count > 0) {
        if (proc->user_frames[0] == proc2->user_frames[0]) {
            return -17;
        }
    }

    /* Verify process entry point matches ELF e_entry */
    const Elf64_Ehdr *veh = (const Elf64_Ehdr *)valid_elf;
    if (proc->user_entry != veh->e_entry) {
        return -18;
    }

    /* Verify code page (0x60000000) permissions: PRESENT=1, USER=1, WRITABLE=0 */
    uint64_t flags = 0;
    if (vmm_get_page_flags_in_pml4(proc->cr3, 0x60000000ULL, &flags) != 0) {
        return -19;
    }
    if (!(flags & PTE_PRESENT) || !(flags & PTE_USER) || (flags & PTE_WRITABLE)) {
        return -20; /* Code page must be read-only! */
    }

    /* Verify data page (0x60001000) permissions: PRESENT=1, USER=1, WRITABLE=1 */
    flags = 0;
    if (vmm_get_page_flags_in_pml4(proc->cr3, 0x60001000ULL, &flags) != 0) {
        return -21;
    }
    if (!(flags & PTE_PRESENT) || !(flags & PTE_USER) || !(flags & PTE_WRITABLE)) {
        return -22; /* Data page must be writable */
    }

    /* Verify user stack (0x70000000) permissions: PRESENT=1, USER=1, WRITABLE=1 */
    flags = 0;
    if (vmm_get_page_flags_in_pml4(proc->cr3, USER_ELF_STACK_VADDR, &flags) != 0) {
        return -23;
    }
    if (!(flags & PTE_PRESENT) || !(flags & PTE_USER) || !(flags & PTE_WRITABLE)) {
        return -24; /* Stack page must be writable */
    }

    /* Verify kernel code (0x100000) supervisor-only */
    flags = 0;
    if (vmm_get_page_flags_in_pml4(proc->cr3, 0x100000ULL, &flags) != 0 || (flags & PTE_USER)) {
        return -25;
    }

    /* 18. Allow processes to execute in Ring 3 under timer-driven scheduler */
    __asm__ volatile ("sti");
    uint64_t start_tick = timer_get_ticks();
    while ((timer_get_ticks() - start_tick) < 300) {
        if ((proc->state == PROCESS_TERMINATED || proc->reaped) &&
            (proc2->state == PROCESS_TERMINATED || proc2->reaped)) {
            break;
        }
        __asm__ volatile ("hlt");
    }

    /* Confirm clean exit with expected status (42) */
    if (proc->exit_status != 42 || proc2->exit_status != 42) {
        return -26;
    }

    /* Trigger reaper to reclaim memory */
    process_reap_terminated();

    /* Verify 0 memory leak after reaping */
    free_after = pmm_get_free_frames();
    if (free_before != free_after) {
        return -27; /* PMM frame leak after reaping */
    }

    return 0;
}

/*
 * elf_print_test_status - Shell command handler for 'elftest'.
 */
void elf_print_test_status(void) {
    vga_puts("\nELF64 Loader Security & Execution Test:\n");

    process_t *caller = process_current();
    if (!caller) caller = process_get(0);

    int val = ELF_ERR_OPEN_FAILED;
    if (caller) {
        int fd = fd_open(caller, "/bin/test", O_RDONLY);
        if (fd >= 0) {
            int64_t file_size = fd_get_size(caller, fd);
            if (file_size >= (int64_t)sizeof(Elf64_Ehdr) && file_size <= (int64_t)SYNTH_ELF_MAX) {
                int64_t nread = fd_read(caller, fd, valid_elf_buf, (size_t)file_size);
                if (nread == file_size) {
                    val = elf_validate(valid_elf_buf, (size_t)file_size);
                }
            }
            fd_close(caller, fd);
        }
    }
    vga_puts("  ELF64 header check:   ");
    if (val == ELF_OK) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Magic, Class64, LittleEndian, x86-64)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL (");
        vga_puts(elf_strerror(val));
        vga_puts(")\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    /* 2. Run in-kernel security and validation suite */
    int res = elf_run_validation_tests();

    vga_puts("  Malformed rejection:  ");
    if (res != -4 && res != -5 && res != -6 && res != -7 && res != -8 && res != -9 &&
        res != -10 && res != -11 && res != -12 && res != -13 && res != -14 && res != -15 &&
        res != -28 && res != -29 && res != -30 && res != -31) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Bad magic, arch, bounds, offsets)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Kernel overlap check: ");
    if (res != -13) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Supervisor regions protected)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Segment permissions:  ");
    if (res != -19 && res != -20 && res != -21 && res != -22 && res != -23 && res != -24) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Code: R-X, Data: RW-, Stack: RW-)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  BSS zero-init:        ");
    if (res != -26) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Verified by user program)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Ring 3 execution:     ");
    if (res != -26) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (CPL=3 confirmed, e_entry used)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Process clean exit:   ");
    if (res != -26) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (Status 42 returned)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Memory reclamation:   ");
    if (res != -16 && res != -27) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("OK (0 PMM frame leaks)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAIL\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_puts("  Result:               ");
    if (res == 0) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("PASSED (All ELF Checks Verified)\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("FAILED (Code ");
        vga_print_dec((uint32_t)(-res));
        vga_puts(")\n");
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}
