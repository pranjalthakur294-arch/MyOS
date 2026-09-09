#ifndef RAMFS_H
#define RAMFS_H

#include "vfs.h"

/*
 * Public RAMFS API
 */
vfs_fs_t *ramfs_create_fs(void);
void ramfs_destroy_fs(vfs_fs_t *fs);

#endif /* RAMFS_H */
