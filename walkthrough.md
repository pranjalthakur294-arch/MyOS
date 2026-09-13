# Stage 12D Walkthrough: Filesystem Mounting Subsystem

## 1. Executive Summary

Stage 12D introduces a clean, modular filesystem mounting abstraction to MyOS. It decouples the Virtual Filesystem (VFS) layer from specific filesystem implementations (`ramfs`, `pfs`) and establishes a formal mount table and filesystem type registry:

1. **Filesystem Type Abstraction (`fs_type_t`)**:
   - Registered driver table with name, driver flags (`FS_REQUIRES_DEV`), and lifecycle operations (`mount`, `unmount`).
   - Dynamic registry via `fs_register_type()` and `fs_find_type()` supporting up to `MAX_FS_TYPES = 8` drivers.
   - Built-in adapters for `ramfs` (in-memory) and `pfs` (persistent disk filesystem).

2. **Filesystem Instance & Mount Record Abstraction**:
   - `fs_instance_t`: Generic instance encapsulating driver reference, device reference, root vnode pointer, and private driver data (`pfs_volume_t` / `ramfs_state_t`).
   - `mount_entry_t`: Mount table entry linking mount point path, mounted `fs_instance_t`, and backing device string.
   - Bounded static mount table: `MAX_MOUNTS = 8` slots pre-allocated in `.bss`, with deterministic slot recycling upon unmount.

3. **Mount / Unmount Lifecycle Operations (`vfs_mount`, `vfs_unmount`)**:
   - Path normalization: strips trailing slashes (e.g., `/disk/` -> `/disk`).
   - Validation sequence: validates filesystem type existence, device requirement, mount point resolution (must resolve to an existing VFS directory), and rejects duplicate mounts.
   - Atomic rollback: if driver `mount` fails, mount record and private state are completely reverted.
   - Unmount protection: root `"/"` mount cannot be unmounted (`MOUNT_ERR_BUSY`).
   - Double unmount prevention: unmounting an inactive or unknown mount point returns `MOUNT_ERR_NOT_FOUND`.

4. **Zero-Allocation Boot Heap Pristineness**:
   - The mount point `/disk` is statically defined in `src/kernel/ramfs.c` `.bss` alongside `/bin`, `/etc`, and `/readme.txt`.
   - Boot-time dynamic heap usage remains strictly `0 bytes` (65512 bytes free, 1 block), guaranteeing 100% regression compatibility with Stages 6–8A.

5. **Shell Commands (`src/kernel/shell.c`)**:
   - `mount`: Lists all active mount points with filesystem type, device name, and root pointer; or mounts a filesystem: `mount <fstype> <dev> <target>`.
   - `umount`: Unmounts a mounted target: `umount <target>`.
   - `mounttest`: Runs the comprehensive 22-step in-kernel verification suite.
   - Screen budget preserved: exactly 42 entries in `commands[]` maintaining 2-column layout (21 rows + 1 header = 22 rows, total 24 rows with prompt), adhering strictly to the screen height limit.

6. **Strict Scope Boundary**:
   - Does NOT cross mount points during VFS path lookup or access `/disk/file.txt` through VFS (strictly reserved for Stage 12E).

---

## 2. Layering Architecture

```text
                  +-----------------------------------+
                  |        User Shell / Tasks         |
                  |     (mount, umount, mounttest)    |
                  +-----------------+-----------------+
                                    |
                                    v
                  +-----------------------------------+
                  |         VFS Mount Manager         |
                  |       (mount.h / mount.c)         |
                  |   - Mount Table (MAX_MOUNTS = 8)  |
                  |   - Type Registry (MAX_FS = 8)    |
                  +---------+---------------+---------+
                            |               |
             +--------------+               +---------------+
             v                                              v
+------------------------+                     +------------------------+
|   RAMFS Type Adapter   |                     |    PFS Type Adapter    |
|   (ramfs_mount_op)     |                     |    (pfs_mount_op)      |
|   root mount: "/"      |                     |    mount point: "/disk"|
+-----------+------------+                     +-----------+------------+
            |                                              |
            v                                              v
+------------------------+                     +------------------------+
|      RAMFS Core        |                     |        PFS Core        |
| (in-memory vnode tree) |                     |  (on-disk superblock,  |
+------------------------+                     |   bitmaps, inodes)     |
                                               +-----------+------------+
                                                           |
                                                           v
                                               +------------------------+
                                               |  Generic Block Device  |
                                               |    (block.h, "ata0")   |
                                               +-----------+------------+
                                                           |
                                                           v
                                               +------------------------+
                                               |    ATA PIO Driver      |
                                               |       (ata.c)          |
                                               +-----------+------------+
                                                           |
                                                           v
                                               +------------------------+
                                               |   Hardware Disk (IDE)  |
                                               +------------------------+
```

---

## 3. In-Kernel Verification Suite (`mounttest`)

The `mounttest` command executes 22 assertions validating error conditions and state transitions:

| Check # | Description | Expected Result |
|---|---|---|
| 1 | `fs_find_type(NULL)` | Returns `NULL` |
| 2 | `fs_find_type("")` | Returns `NULL` |
| 3 | `fs_find_type("nonexistent")` | Returns `NULL` |
| 4 | `fs_find_type("ramfs")` | Returns valid pointer |
| 5 | `fs_find_type("pfs")` | Returns valid pointer |
| 6 | `fs_register_type(NULL)` | Returns `MOUNT_ERR_INVAL` |
| 7 | Re-registering existing `"pfs"` | Returns `MOUNT_ERR_EXISTS` |
| 8 | `mount_find(NULL)` | Returns `NULL` |
| 9 | `mount_find("nonexistent")` | Returns `NULL` |
| 10 | `mount_find("/")` | Finds root mount entry |
| 11 | `vfs_mount(NULL, ...)` | Returns `MOUNT_ERR_INVAL` |
| 12 | `vfs_mount("invalid_fs", ...)` | Returns `MOUNT_ERR_NO_FS` |
| 13 | `vfs_mount("pfs", NULL, "/disk")` | Returns `MOUNT_ERR_NO_DEV` |
| 14 | `vfs_mount("pfs", "invalid_dev", "/disk")` | Returns `MOUNT_ERR_DEV_NOT_FOUND` |
| 15 | `vfs_mount("pfs", "ata0", "/nonexistent")` | Returns `MOUNT_ERR_NOT_FOUND` |
| 16 | `vfs_unmount(NULL)` | Returns `MOUNT_ERR_INVAL` |
| 17 | `vfs_unmount("/")` | Returns `MOUNT_ERR_BUSY` (protected) |
| 18 | `vfs_unmount("/disk")` when unmounted | Returns `MOUNT_ERR_NOT_FOUND` |
| 19 | Mount `"pfs"` on `"ata0"` at `"/disk"` | Returns `MOUNT_OK` |
| 20 | Duplicate mount on `"/disk"` | Returns `MOUNT_ERR_ALREADY_MOUNTED` |
| 21 | Unmount `"/disk"` | Returns `MOUNT_OK` |
| 22 | Slot reuse: Re-mount `"pfs"` on `"/disk"` | Returns `MOUNT_OK` |

---

## 4. Verification & Quality Gates

### Build Quality
- Toolchain: `x86_64-linux-gnu-gcc -std=c99 -Wall -Wextra -O2`
- **Compiler Warnings**: 0
- **Linker Warnings**: 0
- **Git Whitespace Errors (`git diff --check`)**: 0

### Automated Test Matrix
All 22 test suites passing 100%:
- `test_stage12d.py`: **PASS (15/15 tests)**
  - Test 1: Silent boot within screen budget
  - Test 2: Shell help screen fits within 24 non-empty lines
  - Test 3: In-kernel unit check suite (`mounttest` 22/22 assertions)
  - Test 4: Root mount listing (`mount`)
  - Test 5: Clean mount lifecycle (`mount pfs ata0 /disk`)
  - Test 6: Duplicate mount rejection
  - Test 7: Invalid filesystem rejection
  - Test 8: Invalid device rejection
  - Test 9: Unresolvable mount point path rejection
  - Test 10: Clean unmount lifecycle (`umount /disk`)
  - Test 11: Root unmount protection (`umount /` busy)
  - Test 12: Double unmount rejection
  - Test 13: Mount slot reuse across mount/unmount cycles
  - Test 14: Cross-reboot persistence with formatted PFS
  - Test 15: Disk absence handling & coexistence with Stages 1–12C
- Regression suites (Stages 3A through 12C, plus keyboard): **21/21 PASS**
  - Total Regression Score: **22/22 (100%)**
