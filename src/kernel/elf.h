#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * ELF Identification Indices (e_ident)
 */
#define EI_MAG0        0   /* File identification byte 0 */
#define EI_MAG1        1   /* File identification byte 1 */
#define EI_MAG2        2   /* File identification byte 2 */
#define EI_MAG3        3   /* File identification byte 3 */
#define EI_CLASS       4   /* File class (32-bit or 64-bit) */
#define EI_DATA        5   /* Data encoding (endianness) */
#define EI_VERSION     6   /* File version */
#define EI_OSABI       7   /* Operating system / ABI */
#define EI_ABIVERSION  8   /* ABI version */
#define EI_NIDENT      16  /* Size of e_ident array */

/* ELF Magic Numbers */
#define ELFMAG0        0x7F
#define ELFMAG1        'E'
#define ELFMAG2        'L'
#define ELFMAG3        'F'

/* ELF File Classes (e_ident[EI_CLASS]) */
#define ELFCLASSNONE   0
#define ELFCLASS32     1   /* 32-bit architecture */
#define ELFCLASS64     2   /* 64-bit architecture */

/* ELF Endianness (e_ident[EI_DATA]) */
#define ELFDATANONE    0
#define ELFDATA2LSB    1   /* 2's complement, little-endian */
#define ELFDATA2MSB    2   /* 2's complement, big-endian */

/* ELF Machine Architectures (e_machine) */
#define EM_NONE        0
#define EM_386         3   /* Intel 80386 */
#define EM_X86_64      62  /* AMD x86-64 architecture */

/* ELF Object File Types (e_type) */
#define ET_NONE        0
#define ET_REL         1   /* Relocatable file */
#define ET_EXEC        2   /* Executable file */
#define ET_DYN         3   /* Shared object / PIE */
#define ET_CORE        4   /* Core file */

/* ELF Version (e_version & e_ident[EI_VERSION]) */
#define EV_NONE        0
#define EV_CURRENT     1

/* Program Header Segment Types (p_type) */
#define PT_NULL        0
#define PT_LOAD        1   /* Loadable segment */
#define PT_DYNAMIC     2   /* Dynamic linking information */
#define PT_INTERP      3   /* Program interpreter pathname */
#define PT_NOTE        4   /* Auxiliary information */
#define PT_SHLIB       5   /* Reserved */
#define PT_PHDR        6   /* Program header table */
#define PT_TLS         7   /* Thread-local storage */
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK   0x6474e551
#define PT_GNU_RELRO   0x6474e552

/* Program Header Segment Flags (p_flags) */
#define PF_X           (1U << 0)  /* Execute permission */
#define PF_W           (1U << 1)  /* Write permission */
#define PF_R           (1U << 2)  /* Read permission */

/* Maximum program headers parsed per ELF image */
#define MAX_ELF_PHNUM  64

/*
 * User Virtual Address Space Boundaries for ELF Loading
 *
 * Kernel memory:
 *   0x00000000 .. 0x3FFFFFFF (0 to 1 GiB boot identity map, supervisor)
 *   0x50000000 .. 0x501FFFFF (Kernel Heap, supervisor)
 *
 * User memory:
 *   0x40000000 .. 0x7FFFFFFF (VMM region excluding kernel heap)
 *   0x70000000 .. 0x70001000 (Dedicated 4 KiB user stack, stack top 0x70001000)
 */
#define USER_SPACE_MIN         0x40000000ULL
#define USER_SPACE_MAX         0x80000000ULL
#define KERNEL_HEAP_START      0x50000000ULL
#define KERNEL_HEAP_END        0x50200000ULL
#define USER_ELF_STACK_VADDR   0x70000000ULL
#define USER_ELF_STACK_TOP     0x70001000ULL

/*
 * ELF64 File Header (Elf64_Ehdr)
 */
typedef struct {
    unsigned char e_ident[EI_NIDENT]; /* ELF identification bytes */
    uint16_t      e_type;             /* Object file type (ET_EXEC) */
    uint16_t      e_machine;          /* Target machine architecture (EM_X86_64) */
    uint32_t      e_version;          /* File version (EV_CURRENT) */
    uint64_t      e_entry;            /* Entry point virtual address */
    uint64_t      e_phoff;            /* Program header table file offset */
    uint64_t      e_shoff;            /* Section header table file offset */
    uint32_t      e_flags;            /* Processor-specific flags */
    uint16_t      e_ehsize;           /* ELF header size in bytes */
    uint16_t      e_phentsize;        /* Program header entry size in bytes */
    uint16_t      e_phnum;            /* Number of entries in program header table */
    uint16_t      e_shentsize;        /* Section header entry size in bytes */
    uint16_t      e_shnum;            /* Number of entries in section header table */
    uint16_t      e_shstrndx;         /* Section header string table index */
} __attribute__((packed)) Elf64_Ehdr;

/*
 * ELF64 Program Header (Elf64_Phdr)
 */
typedef struct {
    uint32_t      p_type;             /* Segment type (PT_LOAD, etc.) */
    uint32_t      p_flags;            /* Segment attributes (PF_R, PF_W, PF_X) */
    uint64_t      p_offset;           /* Segment file offset */
    uint64_t      p_vaddr;            /* Segment virtual address in memory */
    uint64_t      p_paddr;            /* Segment physical address (reserved/unused) */
    uint64_t      p_filesz;           /* Segment size in file image (bytes) */
    uint64_t      p_memsz;            /* Segment size in memory (bytes) */
    uint64_t      p_align;            /* Segment alignment constraints */
} __attribute__((packed)) Elf64_Phdr;

/*
 * ELF Loader Error Codes
 */
#define ELF_OK                             0
#define ELF_ERR_NULL_IMAGE                -1
#define ELF_ERR_TOO_SMALL                 -2
#define ELF_ERR_BAD_MAGIC                 -3
#define ELF_ERR_NOT_64BIT                 -4
#define ELF_ERR_NOT_LITTLE_ENDIAN         -5
#define ELF_ERR_WRONG_MACHINE             -6
#define ELF_ERR_NOT_EXEC                  -7
#define ELF_ERR_BAD_VERSION               -8
#define ELF_ERR_BAD_EHSIZE                -9
#define ELF_ERR_BAD_PHENTSIZE             -10
#define ELF_ERR_BAD_PHNUM                 -11
#define ELF_ERR_PHDR_OUT_OF_BOUNDS        -12
#define ELF_ERR_FILESZ_GREATER_THAN_MEMSZ -13
#define ELF_ERR_FILE_OFFSET_OVERFLOW      -14
#define ELF_ERR_FILE_BOUNDS_VIOLATION     -15
#define ELF_ERR_VADDR_OVERFLOW            -16
#define ELF_ERR_KERNEL_OVERLAP            -17
#define ELF_ERR_USER_BOUNDS_VIOLATION     -18
#define ELF_ERR_STACK_OVERLAP             -19
#define ELF_ERR_NO_LOAD_SEGMENTS          -20
#define ELF_ERR_SEGMENT_OVERLAP           -21
#define ELF_ERR_ENTRY_NOT_EXECUTABLE      -22
#define ELF_ERR_INVALID_ENTRY             -23
#define ELF_ERR_NO_MEMORY                 -24
#define ELF_ERR_TABLE_LIMIT               -25
#define ELF_ERR_MAP_FAILED                -26
#define ELF_ERR_BAD_ALIGNMENT             -27

/* Embedded user-space ELF image symbols exported by elf_image.S */
extern const uint8_t _binary_test_program_elf_start[];
extern const uint8_t _binary_test_program_elf_end[];
extern const uint64_t _binary_test_program_elf_size;

struct process; /* Forward declaration */

/*
 * ELF Loader Public API
 */
void elf_init(void);
int elf_validate(const void *image, size_t size);
const char *elf_strerror(int err);
int elf_load_into_process(struct process *proc, const void *image, size_t size, uint64_t *out_entry);
struct process *process_create_from_elf(const void *image, size_t size, const char *name);

/*
 * Stage 10 Security & Validation In-Kernel Test Harness
 */
int elf_run_validation_tests(void);
void elf_print_test_status(void);

#endif /* ELF_H */
