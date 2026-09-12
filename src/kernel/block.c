#include "block.h"

static block_device_t *s_devices[MAX_BLOCK_DEVICES] = {0};
static size_t s_device_count = 0;
static bool s_block_initialized = false;

/* Helper: string comparison */
static bool streq(const char *a, const char *b) {
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/*
 * block_init - Initializes the generic block device registry.
 * Explicitly idempotent.
 */
int block_init(void) {
    for (size_t i = 0; i < MAX_BLOCK_DEVICES; i++) {
        s_devices[i] = NULL;
    }
    s_device_count = 0;
    s_block_initialized = true;
    return BLOCK_OK;
}

/*
 * block_register - Registers a block device into the bounded static registry.
 */
int block_register(block_device_t *device) {
    if (!s_block_initialized) {
        block_init();
    }
    if (!device) {
        return BLOCK_ERR_INVALID;
    }
    if (device->name[0] == '\0') {
        return BLOCK_ERR_INVALID;
    }
    if (device->sector_size == 0 || device->sector_count == 0) {
        return BLOCK_ERR_INVALID;
    }
    if (!device->read || !device->write) {
        return BLOCK_ERR_INVALID;
    }
    if (device->registered) {
        return BLOCK_ERR_EXIST;
    }

    /* Check for duplicate name */
    for (size_t i = 0; i < s_device_count; i++) {
        if (s_devices[i] && streq(s_devices[i]->name, device->name)) {
            return BLOCK_ERR_EXIST;
        }
    }

    /* Check capacity */
    if (s_device_count >= MAX_BLOCK_DEVICES) {
        return BLOCK_ERR_NOMEM;
    }

    s_devices[s_device_count++] = device;
    device->registered = true;
    return BLOCK_OK;
}

/*
 * block_unregister - Removes a registered block device from the registry.
 */
int block_unregister(block_device_t *device) {
    if (!device || !device->registered) {
        return BLOCK_ERR_INVALID;
    }

    for (size_t i = 0; i < s_device_count; i++) {
        if (s_devices[i] == device) {
            /* Shift subsequent entries left */
            for (size_t j = i; j + 1 < s_device_count; j++) {
                s_devices[j] = s_devices[j + 1];
            }
            s_device_count--;
            s_devices[s_device_count] = NULL;
            device->registered = false;
            return BLOCK_OK;
        }
    }

    return BLOCK_ERR_NOT_FOUND;
}

/*
 * block_get - Looks up a registered block device by its name.
 */
block_device_t *block_get(const char *name) {
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < s_device_count; i++) {
        if (s_devices[i] && streq(s_devices[i]->name, name)) {
            return s_devices[i];
        }
    }
    return NULL;
}

/*
 * block_get_by_index - Retrieves device at specified registry index.
 */
block_device_t *block_get_by_index(size_t index) {
    if (index >= s_device_count) {
        return NULL;
    }
    return s_devices[index];
}

/*
 * block_device_count - Returns the total count of registered block devices.
 */
size_t block_device_count(void) {
    return s_device_count;
}

/*
 * block_read - Validates arguments and dispatches a single-sector read.
 */
int block_read(block_device_t *device, uint32_t sector, void *buffer) {
    if (!device || !device->registered) {
        return BLOCK_ERR_INVALID;
    }
    if (!buffer) {
        return BLOCK_ERR_INVALID;
    }
    if (device->sector_size == 0) {
        return BLOCK_ERR_INVALID;
    }
    if (sector >= device->sector_count) {
        return BLOCK_ERR_RANGE;
    }
    if (!device->read) {
        return BLOCK_ERR_INVALID;
    }

    return device->read(device, sector, buffer);
}

/*
 * block_write - Validates arguments and dispatches a single-sector write.
 */
int block_write(block_device_t *device, uint32_t sector, const void *buffer) {
    if (!device || !device->registered) {
        return BLOCK_ERR_INVALID;
    }
    if (!buffer) {
        return BLOCK_ERR_INVALID;
    }
    if (device->sector_size == 0) {
        return BLOCK_ERR_INVALID;
    }
    if (sector >= device->sector_count) {
        return BLOCK_ERR_RANGE;
    }
    if (!device->write) {
        return BLOCK_ERR_INVALID;
    }

    return device->write(device, sector, buffer);
}

/*
 * block_sector_size - Returns sector size in bytes.
 */
uint32_t block_sector_size(const block_device_t *device) {
    if (!device) {
        return 0;
    }
    return device->sector_size;
}

/*
 * block_sector_count - Returns total sector count.
 */
uint32_t block_sector_count(const block_device_t *device) {
    if (!device) {
        return 0;
    }
    return device->sector_count;
}

/*
 * block_name - Returns device name string.
 */
const char *block_name(const block_device_t *device) {
    if (!device) {
        return "";
    }
    return device->name;
}
