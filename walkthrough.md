# Stage 12E Walkthrough: VFS -> Persistent Filesystem Integration

## 1. Executive Summary

Stage 12E elevates the filesystem mounting subsystem into an active VFS path-resolution boundary. Prior to this stage, the mount table maintained registration records, but VFS path traversal was unaware of mount points. With Stage 12E, the operating system achieves complete, transparent integration across all storage layers:

```text
User / Shell / Tasks
         |
    VFS Layer (vfs.h, vfs.c)
         |
  Mount Resolution Boundary
   |                     |
   v                     v
RAMFS ("/")         PFS ("/disk")
                         |
                Block Device API (block.h)
                         |
                ATA Adapter (ata.c)
                         |
                ATA PIO Driver
                         |
                QEMU Disk (disk.img)
```

Key Accomplishments:
1. **Generic Mount Boundary Traversal**:
   - `vfs_lookup_from` resolves path components dynamically across mounts without any hardcoded paths.
   - Forward crossing: resolving a component matching an active mountpoint directory crosses directly into the mounted filesystem instance's root vnode.
   - Reverse `..` crossing: resolving `..` from a mounted filesystem root ascends back out to the parent directory of the host mountpoint, while root `..` at `/` remains strictly clamped at `/`.
   - Canonical path reconstruction: `vfs_get_path` ascends through mount roots back into host mountpoint nodes, accurately reconstructing `/disk` and subdirectories (`/disk/subdir`).
2. **Distinct Mount Root Semantics**:
   - Host mountpoint vnodes (in RAMFS) and mounted root vnodes (in PFS) remain separate, distinct objects in memory.
   - The mount entry maintains an active reference (`vfs_node_ref`) to the host mountpoint node throughout the mount lifecycle.
3. **PFS VFS Adapter**:
   - Implemented a complete `vfs_node_ops_t` table for PFS: `read`, `write`, `lookup`, `create`, `mkdir`, `readdir`, `unlink`, and `release`.
   - Bounded static vnode pool (`s_pfs_vnodes[32]`) in `.bss`; zero dynamic allocations during boot or unreferenced caching.
   - On-disk file unlinking (`pfs_unlink`): reclaims data blocks, frees inode in bitmap, zeroes directory entry on disk, and synchronizes superblock.
   - Directory entry iteration (`pfs_readdir_entry`): extracts entry name, type, and size by index.
4. **CWD & File Descriptor Compatibility**:
   - Processes can set CWD inside mounted filesystems (`cd /disk`, `pwd` reports `/disk`).
   - Relative paths resolve from CWD within PFS (`cat msg.txt`, `touch file.txt`).
   - File descriptors (`fd_open`, `fd_read`, `fd_write`, `fd_close`) operate transparently on PFS files through VFS.
5. **Busy Unmount Protection & Lifetime Safety**:
   - `mount_check_busy()` rejects unmount with `MOUNT_ERR_BUSY` if root `ref_count > 1` (e.g., process CWD inside mount) or if any child vnodes are actively held (e.g., open file descriptors).
   - Releasing all descriptors and navigating CWD out of the mountpoint enables clean unmount and subsequent remount.
6. **Cross-Boot Persistence**:
   - Verified real multi-session persistence across separate QEMU boot instances.

---

## 2. In-Kernel Verification Suite (`vfs12etest`)

The `vfs12etest` shell command executes a 15-assertion in-kernel verification suite:

| Check # | Description | Expected Result | Verified |
|---|---|---|---|
| 1 | Mountpoint path resolution to PFS root | `vfs_lookup("/disk") == disk_mnt->instance->root` | PASS |
| 2 | Distinct mount root semantics | Mountpoint node != Mounted root node; Inode == 1 | PASS |
| 3 | Create file via VFS in mounted PFS | `vfs_create("/disk/test12e.txt") == VFS_OK` | PASS |
| 4 | Write & read file via VFS ops | 32-byte payload roundtrip byte-for-byte | PASS |
| 5 | Create directory via VFS in mounted PFS | `vfs_mkdir("/disk/dir12e") == VFS_OK` | PASS |
| 6 | Create & read file inside subdirectory | `vfs_create("/disk/dir12e/sub.txt")` and read roundtrip | PASS |
| 7 | Directory iteration via `vfs_readdir` | Finds `test12e.txt` (file) and `dir12e` (dir) | PASS |
| 8 | Cross-boundary `..` traversal | `vfs_lookup("/disk/..") == vfs_root` & `../readme.txt` | PASS |
| 9 | Root `..` clamping | `vfs_lookup("/..") == vfs_root` | PASS |
| 10 | Canonical path reconstruction | `vfs_get_path` -> `"/disk"`, `"/disk/dir12e"` | PASS |
| 11 | CWD relative resolution in mount | CWD at `/disk` resolves `test12e.txt` & `../readme.txt` | PASS |
| 12 | FD open/write/read/close on PFS file | `fd_open("/disk/test12e.txt", O_RDWR)` -> `fd_read` | PASS |
| 13 | Busy unmount rejection | Rejected with `MOUNT_ERR_BUSY` while FD open & CWD in mount | PASS |
| 14 | Clean unmount & remount persistence | File persists across unmount/remount cycle | PASS |
| 15 | Cleanup test artifacts via VFS unlink | `vfs_unlink` removes files and frees resources | PASS |

---

## 3. Automated Test Matrix (`test_stage12e.py`)

Dedicated test suite verifying 15 automated scenarios across QEMU instances:

- **Test 1 (Boot Integrity and 25-Row Budget)**: PASS
- **Test 2 (Shell Command Table Layout & 24-Row Budget)**: PASS
- **Test 3 ('vfs12etest' In-Kernel Suite Execution)**: PASS (15/15 checks)
- **Test 4 (File Creation via VFS: `touch /disk/shell_test.txt`)**: PASS
- **Test 5 (Write & Cat via VFS/FD: `writefile /disk/msg.txt HelloFrom12E`)**: PASS
- **Test 6 (Subdirectory & Subfile Operations)**: PASS
- **Test 7 (Directory Iteration: `ls /disk`)**: PASS
- **Test 8 (CWD Integration into Mount: `cd /disk; pwd`)**: PASS
- **Test 9 (Relative Path Operations from CWD in Mount)**: PASS
- **Test 10 (Cross-Boundary `..` Traversal: `cat ../readme.txt`)**: PASS
- **Test 11 (Root `..` Clamping: `cd /; cd ..; pwd`)**: PASS
- **Test 12 (Return to Parent via `cd ..`: `cd /disk; cd ..; pwd`)**: PASS
- **Test 13 (File Deletion via VFS: `rm /disk/shell_test.txt`)**: PASS
- **Test 14 (Busy Unmount Rejection When CWD Inside Mount)**: PASS
- **Test 15 (Cross-Boot Persistence Across Separate QEMU Sessions)**: PASS

---

## 4. Full Regression Summary

- `test_stage12a.py`: **8/8 PASS** (100%)
- `test_stage12b.py`: **8/8 PASS** (100%)
- `test_stage12c.py`: **10/10 PASS** (100%)
- `test_stage12d.py`: **15/15 PASS** (100%)
- `test_stage12e.py`: **15/15 PASS** (100%)
- **Total Stage 12 Matrix**: **56/56 PASS (100%)**
- **Compiler Warnings**: 0
- **Linker Warnings**: 0
- **Boot Heap Invariant**: `Used: 0 bytes`, `Free: 65512 bytes` (100% preserved)
- **Screen Budget**: All outputs fit strictly within 24 rows
