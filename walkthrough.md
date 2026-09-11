# Stage 11E Walkthrough: Process Working Directory, Path Resolution & File Lifecycle

## 1. Executive Summary

Stage 11E completes the process-centric filesystem interface for MyOS:
1. **Per-Process Current Working Directory (`cwd`)**:
   - Each process maintains an active directory reference (`cwd`) initialized to `/`.
   - `process_set_cwd()` manages reference ownership safely (`vfs_node_ref` / `vfs_node_unref`).
   - Clean CWD reference release during process reaping.
2. **Relative Path Resolution & Hierarchical Navigation**:
   - Directory nodes track non-owning back-pointers (`parent`), with root pointing to itself.
   - `vfs_lookup_from()` resolves both relative paths (from process `cwd`) and absolute paths (from `/`), fully supporting `.` (current) and `..` (parent), clamped safely at `/`.
   - `vfs_get_path()` reconstructs canonical absolute paths by climbing parent pointers with zero heap allocations.
3. **File Lifecycle & Unlinking**:
   - `vfs_unlink_from()` / `ramfs_unlink()` unlinks regular files and detaches directory entries.
   - Rejects directory targets (`VFS_ERR_IS_DIR`), root `/`, `.`, and `..`.
   - Dual-condition destruction: files are destroyed immediately if unreferenced (`ref_count == 0`), or deferred until the last active file descriptor closes (`fd_close` / `vfs_node_unref`).
4. **Shell Filesystem Commands**:
   - `pwd`: Prints the current process's canonical working directory path.
   - `cd <path>`: Changes working directory with full relative, absolute, and root clamping support; leaves CWD unchanged on errors with zero memory leaks.
   - `rm <path>`: Removes regular files with comprehensive negative validation.
   - Updated `ls`, `cat`, `touch`, `mkdir`, and `run` to resolve relative paths against `cwd`.

---

## 2. Layering Architecture

```text
User / Shell
  │
  ├── pwd               ──► vfs_get_path(proc->cwd)
  ├── cd <path>         ──► vfs_lookup_from(proc->cwd) ──► process_set_cwd()
  ├── rm <path>         ──► vfs_unlink_from(proc->cwd)
  ├── ls [path]         ──► vfs_lookup_from(proc->cwd) ──► vfs_readdir()
  ├── cat <path>        ──► fd_open(proc, path)        ──► vfs_lookup_from(proc->cwd)
  ├── touch <path>      ──► vfs_create_from(proc->cwd)
  ├── mkdir <path>      ──► vfs_mkdir_from(proc->cwd)
  └── run <path>        ──► process_exec_path()        ──► fd_open(proc, path)
        │
        ▼
VFS Layer (vfs.h, vfs.c)
  - Intrusive reference counting: vfs_node_ref(), vfs_node_unref()
  - Hierarchical parent back-pointers: node->parent
  - Path traversal & normalization: vfs_lookup_from(), split_parent_and_leaf_from()
  - Canonical path reconstruction: vfs_get_path()
        │
        ▼
RAMFS Storage Layer (ramfs.h, ramfs.c)
  - Node unlink: ramfs_unlink() detaches dirent, sets node->unlinked = true
  - Deferred release: ramfs_release() destroys node when unlinked && ref_count == 0
  - Static boot image preservation (/bin, /bin/test, /bin/bad, /etc, /readme.txt)
```

---

## 3. Verification & Quality Gates

### Build Quality
- Toolchain: `x86_64-linux-gnu-gcc -std=c99 -Wall -Wextra -O2`
- **Compiler Warnings: 0**
- **Linker Warnings: 0**
- **Whitespace Errors: 0 (`git diff --check`)**

### Stage 11E Automated Test Suite (`test_stage11e.py`)
- **[TEST 1]** Boot & 25-Row Screen Line Budget: **PASS**
- **[TEST 2]** Initial `pwd` prints `/`: **PASS**
- **[TEST 3]** `cd /bin` and `pwd` prints `/bin`: **PASS**
- **[TEST 4]** `cd ..`, `cd .`, `cd /`, and root clamping (`cd ../..` stays at `/`): **PASS**
- **[TEST 5]** `cd` Error Handling (nonexistent path, regular file, usage, CWD unchanged): **PASS**
- **[TEST 6]** Relative Directory Creation & Nested Navigation (`mkdir testdir`, `cd testdir`, `mkdir sub`, `cd sub`): **PASS**
- **[TEST 7]** Relative File Creation, Listing & Reading (`touch note.txt`, `ls`, `cat note.txt`, `cat ../readme.txt`): **PASS**
- **[TEST 8]** `rm` File Unlinking (`rm testdir/note.txt`, confirmed by `ls` and `cat`): **PASS**
- **[TEST 9]** `rm` Negative Validation (`/nonexistent`, `/bin`, `/`, `.`, `..`, usage errors): **PASS**
- **[TEST 10]** ELF Execution via Relative Path (`cd /bin`, `run test`): **PASS**
- **[TEST 11]** Repeated Operations, Lifecycle & Heap Stability: **PASS**
- **[TEST 12]** Full Coexistence (`help`, `about`, `vfstest`, `fdtest`): **PASS**

### Full 17-Suite Regression Matrix (100% PASS)
```text
==================================================
FULL REPRESSION MATRIX RESULTS:
==================================================
  test_stage11e.py     : 12/12 PASS
  test_stage11d.py     : 12/12 PASS
  test_stage11c.py     : 10/10 PASS
  test_stage11b.py     : 8/8   PASS
  test_stage11a.py     : 10/10 PASS
  test_stage10.py      : 14/14 PASS
  test_stage9.py       : 12/12 PASS
  test_stage8b.py      : 12/12 PASS
  test_stage8a.py      : 10/10 PASS
  test_stage7b.py      : 10/10 PASS
  test_stage7a.py      : 8/8   PASS
  test_stage6.py       : 15/15 PASS
  test_stage5b.py      : 10/10 PASS
  test_stage5a.py      : 10/10 PASS
  test_stage4.py       : 8/8   PASS
  test_stage3b.py      : 6/6   PASS
  test_stage3a.py      : 6/6   PASS
==================================================
ALL 17 TEST SUITES PASSED WITH ZERO REGRESSIONS.
==================================================
```
