#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stdbool.h>

/* Primary ATA Bus I/O Ports */
#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1  /* Read */
#define ATA_PRIMARY_FEATURES    0x1F1  /* Write */
#define ATA_PRIMARY_SECCOUNT    0x1F2
#define ATA_PRIMARY_LBA_LOW     0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HIGH    0x1F5
#define ATA_PRIMARY_DRIVE       0x1F6
#define ATA_PRIMARY_STATUS      0x1F7  /* Read */
#define ATA_PRIMARY_COMMAND     0x1F7  /* Write */
#define ATA_PRIMARY_CONTROL     0x3F6  /* Device Control / Alt Status */

/* Status Register Bitmasks */
#define ATA_SR_BSY              0x80  /* Busy */
#define ATA_SR_DRDY             0x40  /* Drive ready */
#define ATA_SR_DF               0x20  /* Drive write fault */
#define ATA_SR_DSC              0x10  /* Drive seek complete */
#define ATA_SR_DRQ              0x08  /* Data request ready */
#define ATA_SR_CORR             0x04  /* Corrected data */
#define ATA_SR_IDX              0x02  /* Index */
#define ATA_SR_ERR              0x01  /* Error */

/* ATA Command Opcodes */
#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_FLUSH_CACHE     0xE7

/* Driver Error Codes */
#define ATA_OK                   0
#define ATA_ERR_ABSENT          -1
#define ATA_ERR_INVALID         -2
#define ATA_ERR_TIMEOUT         -3
#define ATA_ERR_CONTROLLER      -4
#define ATA_ERR_FAULT           -5

/* Sector Properties */
#define ATA_SECTOR_SIZE         512
#define ATA_MAX_LBA28           0x0FFFFFFF
#define ATA_TEST_LBA            8

/* Driver API */
int ata_init(void);
int ata_identify(void);
int ata_read_sector(uint32_t lba, void *buffer);
int ata_write_sector(uint32_t lba, const void *buffer);

/* Informational Getters */
bool ata_is_present(void);
uint32_t ata_get_sector_count(void);
uint32_t ata_get_sector_size(void);
const char *ata_get_model(void);

/* Generic Block Layer Adapter (Stage 12B) */
int ata_block_register(void);

#endif /* ATA_H */
