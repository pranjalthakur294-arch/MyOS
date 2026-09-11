#include "ata.h"
#include "io.h"

#define ATA_TIMEOUT_COUNT 2000000

static bool g_ata_present = false;
static uint32_t g_ata_sector_count = 0;
static uint32_t g_ata_sector_size = ATA_SECTOR_SIZE;
static char g_ata_model[41] = {0};

/*
 * ata_delay - 400ns delay by reading alternate status port 4 times.
 */
static inline void ata_delay(void) {
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
}

/*
 * ata_wait_not_busy - Wait for BSY flag to clear using alternate status.
 */
static int ata_wait_not_busy(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT_COUNT; i++) {
        uint8_t status = inb(ATA_PRIMARY_CONTROL);
        if (!(status & ATA_SR_BSY)) {
            return ATA_OK;
        }
    }
    return ATA_ERR_TIMEOUT;
}

/*
 * ata_wait_ready - Wait for BSY to clear and DRDY to be asserted.
 */
static int ata_wait_ready(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT_COUNT; i++) {
        uint8_t status = inb(ATA_PRIMARY_CONTROL);
        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            return (status & ATA_SR_DF) ? ATA_ERR_FAULT : ATA_ERR_CONTROLLER;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) {
            return ATA_OK;
        }
    }
    return ATA_ERR_TIMEOUT;
}

/*
 * ata_wait_drq - Wait for BSY to clear and DRQ to be asserted.
 */
static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT_COUNT; i++) {
        uint8_t status = inb(ATA_PRIMARY_CONTROL);
        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            return (status & ATA_SR_DF) ? ATA_ERR_FAULT : ATA_ERR_CONTROLLER;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return ATA_OK;
        }
    }
    return ATA_ERR_TIMEOUT;
}

/*
 * ata_identify - Issue IDENTIFY (0xEC) to primary master and read parameters.
 */
int ata_identify(void) {
    g_ata_present = false;
    g_ata_sector_count = 0;
    g_ata_sector_size = ATA_SECTOR_SIZE;
    g_ata_model[0] = '\0';

    /* Select Primary Master */
    outb(ATA_PRIMARY_DRIVE, 0xA0);
    ata_delay();

    /* Clear LBA registers */
    outb(ATA_PRIMARY_SECCOUNT, 0);
    outb(ATA_PRIMARY_LBA_LOW, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HIGH, 0);

    /* Check status for floating bus or absent drive */
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0xFF || status == 0x00) {
        return ATA_ERR_ABSENT;
    }

    /* Send IDENTIFY command */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    status = inb(ATA_PRIMARY_STATUS);
    if (status == 0x00) {
        return ATA_ERR_ABSENT;
    }

    /* Wait for BSY to clear */
    int err = ata_wait_not_busy();
    if (err != ATA_OK) {
        return err;
    }

    /* Check for non-ATA devices (ATAPI or SATA) */
    uint8_t mid = inb(ATA_PRIMARY_LBA_MID);
    uint8_t high = inb(ATA_PRIMARY_LBA_HIGH);
    if (mid != 0 || high != 0) {
        return ATA_ERR_ABSENT;
    }

    /* Wait for DRQ to be ready to transfer 256 words */
    err = ata_wait_drq();
    if (err != ATA_OK) {
        return err;
    }

    /* Read 256 16-bit words */
    uint16_t id_buf[256];
    for (int i = 0; i < 256; i++) {
        id_buf[i] = inw(ATA_PRIMARY_DATA);
    }

    /* Check LBA capability: Word 49 bit 9 */
    if (!(id_buf[49] & (1 << 9))) {
        return ATA_ERR_CONTROLLER;
    }

    /* LBA28 sector count: Words 60 and 61 */
    g_ata_sector_count = (uint32_t)id_buf[60] | ((uint32_t)id_buf[61] << 16);
    g_ata_sector_size = ATA_SECTOR_SIZE;

    /* Model string: Words 27 through 46 (40 characters) */
    int idx = 0;
    for (int i = 27; i <= 46; i++) {
        g_ata_model[idx++] = (char)((id_buf[i] >> 8) & 0xFF);
        g_ata_model[idx++] = (char)(id_buf[i] & 0xFF);
    }
    g_ata_model[40] = '\0';

    /* Trim trailing spaces from model string */
    int len = 40;
    while (len > 0 && (g_ata_model[len - 1] == ' ' || g_ata_model[len - 1] == '\0')) {
        g_ata_model[--len] = '\0';
    }

    g_ata_present = true;
    return ATA_OK;
}

/*
 * ata_read_sector - Read a single 512-byte sector using 28-bit LBA.
 */
int ata_read_sector(uint32_t lba, void *buffer) {
    if (!buffer) {
        return ATA_ERR_INVALID;
    }
    if (!g_ata_present) {
        return ATA_ERR_ABSENT;
    }
    if (lba > ATA_MAX_LBA28 || lba >= g_ata_sector_count) {
        return ATA_ERR_INVALID;
    }

    int err = ata_wait_not_busy();
    if (err != ATA_OK) {
        return err;
    }

    /* Select drive with Master + LBA bits 24..27 */
    outb(ATA_PRIMARY_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay();

    err = ata_wait_ready();
    if (err != ATA_OK) {
        return err;
    }

    /* Program sector count and LBA registers */
    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));

    /* Issue READ SECTORS command */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);
    ata_delay();

    err = ata_wait_drq();
    if (err != ATA_OK) {
        return err;
    }

    /* Read 256 words (512 bytes) from data port */
    uint16_t *buf16 = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        buf16[i] = inw(ATA_PRIMARY_DATA);
    }

    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status & (ATA_SR_ERR | ATA_SR_DF)) {
        return (status & ATA_SR_DF) ? ATA_ERR_FAULT : ATA_ERR_CONTROLLER;
    }

    return ATA_OK;
}

/*
 * ata_write_sector - Write a single 512-byte sector using 28-bit LBA.
 */
int ata_write_sector(uint32_t lba, const void *buffer) {
    if (!buffer) {
        return ATA_ERR_INVALID;
    }
    if (!g_ata_present) {
        return ATA_ERR_ABSENT;
    }
    if (lba > ATA_MAX_LBA28 || lba >= g_ata_sector_count) {
        return ATA_ERR_INVALID;
    }

    int err = ata_wait_not_busy();
    if (err != ATA_OK) {
        return err;
    }

    /* Select drive with Master + LBA bits 24..27 */
    outb(ATA_PRIMARY_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay();

    err = ata_wait_ready();
    if (err != ATA_OK) {
        return err;
    }

    /* Program sector count and LBA registers */
    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));

    /* Issue WRITE SECTORS command */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);
    ata_delay();

    err = ata_wait_drq();
    if (err != ATA_OK) {
        return err;
    }

    /* Write 256 words (512 bytes) to data port */
    const uint16_t *buf16 = (const uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        outw(ATA_PRIMARY_DATA, buf16[i]);
    }

    /* Wait for sector write to complete before issuing FLUSH CACHE */
    err = ata_wait_not_busy();
    if (err != ATA_OK) {
        return err;
    }

    /* Flush drive cache */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_FLUSH_CACHE);
    ata_delay();

    err = ata_wait_not_busy();
    if (err != ATA_OK) {
        return err;
    }

    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status & (ATA_SR_ERR | ATA_SR_DF)) {
        return (status & ATA_SR_DF) ? ATA_ERR_FAULT : ATA_ERR_CONTROLLER;
    }

    return ATA_OK;
}

/*
 * ata_init - Probe and initialize ATA primary master driver quietly.
 */
int ata_init(void) {
    return ata_identify();
}

bool ata_is_present(void) {
    return g_ata_present;
}

uint32_t ata_get_sector_count(void) {
    return g_ata_sector_count;
}

uint32_t ata_get_sector_size(void) {
    return g_ata_sector_size;
}

const char *ata_get_model(void) {
    return g_ata_model;
}
