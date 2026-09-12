#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BLOCK_NAME_MAX      32
#define MAX_BLOCK_DEVICES   8

/* Generic Block Error Codes */
#define BLOCK_OK             0
#define BLOCK_ERR_INVALID   -1   /* NULL pointer, bad argument, invalid sector size */
#define BLOCK_ERR_NOT_FOUND -2   /* Device not found in registry */
#define BLOCK_ERR_IO        -3   /* Underlying controller / hardware I/O error */
#define BLOCK_ERR_NOT_READY -4   /* Device absent, busy, or timed out */
#define BLOCK_ERR_RANGE     -5   /* Sector >= sector_count or out of range */
#define BLOCK_ERR_NOMEM     -6   /* Registry capacity exceeded */
#define BLOCK_ERR_EXIST     -7   /* Device with duplicate name already registered */

/* Device Flags */
#define BLOCK_FLAG_READONLY  (1 << 0)

/* Forward declaration */
typedef struct block_device block_device_t;

/* Callback function pointer types for sector I/O */
typedef int (*block_read_fn)(block_device_t *dev, uint32_t sector, void *buffer);
typedef int (*block_write_fn)(block_device_t *dev, uint32_t sector, const void *buffer);

/* Generic Block Device Descriptor */
struct block_device {
    char name[BLOCK_NAME_MAX];       /* Human-readable identifier e.g. "ata0" */
    uint32_t sector_size;            /* Fixed sector size in bytes (e.g. 512) */
    uint32_t sector_count;           /* Total addressable sectors */
    uint32_t flags;                  /* Device capability flags */
    block_read_fn read;              /* Single-sector read callback */
    block_write_fn write;            /* Single-sector write callback */
    void *priv;                      /* Driver-specific private context pointer */
    bool registered;                 /* Registration state flag */
};

/* Generic Block Subsystem Management API */
int block_init(void);
int block_register(block_device_t *device);
int block_unregister(block_device_t *device);
block_device_t *block_get(const char *name);
block_device_t *block_get_by_index(size_t index);
size_t block_device_count(void);

/* Generic Block I/O Operations */
int block_read(block_device_t *device, uint32_t sector, void *buffer);
int block_write(block_device_t *device, uint32_t sector, const void *buffer);

/* Informational Accessors */
uint32_t block_sector_size(const block_device_t *device);
uint32_t block_sector_count(const block_device_t *device);
const char *block_name(const block_device_t *device);

#endif /* BLOCK_H */
