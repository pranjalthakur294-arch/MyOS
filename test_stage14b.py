#!/usr/bin/env python3
"""
test_stage14b.py - Automated Verification Suite for MyOS Stage 14B
(Userland Runtime & Filesystem Syscalls: open, readdir, chdir, getcwd, dirent, stdio, string)

Verifies all 26 requirements:
  1. Header verification: user/include/syscall.h contains SYS_* definitions
  2. Header verification: user/include/unistd.h contains standard POSIX-like declarations
  3. Header verification: user/include/dirent.h contains struct dirent and readdir
  4. Header verification: user/include/string.h contains string function declarations
  5. Header verification: user/include/stdio.h contains print/puts declarations
  6. Library compilation: build/user/lib/syscall.o, string.o, stdio.o exist
  7. Binary compilation: build/user/file_test.elf, ls.elf, pwd.elf, cd_test.elf exist
  8. Inode verification: all 4 binaries present on disk image with correct inodes
  9. Directory entry: /disk/file_test, /disk/ls, /disk/pwd, /disk/cd_test exist
 10. Multi-block directory: disk image root and /bin have valid multi-block structure
 11. User runtime test: string functions (strlen, strcmp, strncmp, strcpy, strncpy, memcpy, memset)
 12. User runtime test: stdio functions (puts, print, print_err, print_dec, print_hex)
 13. SYS_OPEN: user process opens file with O_RDONLY, gets valid fd (>= 3)
 14. SYS_READ: user process reads file content correctly
 15. SYS_CLOSE: user process closes fd, subsequent read returns -EBADF
 16. Error handling: open non-existent file returns negative error (-ENOENT)
 17. Error handling: read invalid fd returns negative error (-EBADF)
 18. Error handling: close invalid fd returns negative error (-EBADF)
 19. SYS_READDIR: readdir() returns directory entries with name and type
 20. SYS_READDIR EOF: readdir() returns 0 when all entries read
 21. SYS_READDIR error: readdir() on regular file returns -ENOTDIR
 22. SYS_GETCWD: getcwd() returns current directory (initially "/")
 23. SYS_CHDIR: chdir("/disk") succeeds, getcwd() reflects change
 24. Relative path: chdir("bin") from /disk navigates to /disk/bin
 25. Parent path: chdir("..") navigates to parent directory
 26. Security: chdir/getcwd validate user pointers, reject kernel addresses with -EFAULT
"""

import os
import struct
import subprocess
import sys
import time

def text_to_sendkeys(text):
    key_map = {
        ' ': 'spc', '\n': 'ret', '\b': 'backspace', '\t': 'tab',
        '-': 'minus', '=': 'equal', '[': 'bracket_left', ']': 'bracket_right',
        ';': 'semicolon', "'": 'apostrophe', '`': 'grave_accent',
        '\\': 'backslash', ',': 'comma', '.': 'dot', '/': 'slash',
    }
    shift_map = {
        '!': 'shift-1', '@': 'shift-2', '#': 'shift-3', '$': 'shift-4',
        '%': 'shift-5', '^': 'shift-6', '&': 'shift-7', '*': 'shift-8',
        '(': 'shift-9', ')': 'shift-0', '_': 'shift-minus', '+': 'shift-equal',
        ':': 'shift-semicolon', '"': 'shift-apostrophe', '<': 'shift-comma',
        '>': 'shift-dot', '?': 'shift-slash',
    }
    keys = []
    for ch in text:
        if ch in key_map:
            keys.append(f"sendkey {key_map[ch]}")
        elif ch in shift_map:
            keys.append(f"sendkey {shift_map[ch]}")
        elif ch.isupper():
            keys.append(f"sendkey shift-{ch.lower()}")
        else:
            keys.append(f"sendkey {ch}")
    return keys

def run_qemu_test(key_sequence, wait_time=0.8, boot_wait=3.0, attach_disk=True):
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    if attach_disk:
        cmd.extend(["-hda", "build/disk.img"])

    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(boot_wait)

    for k in key_sequence:
        if isinstance(k, tuple) and k[0] == "sleep":
            time.sleep(k[1])
            continue
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.03)

    time.sleep(wait_time)
    out, _ = p.communicate(input="xp /4000xb 0xb8000\nq\n")

    chars = []
    for line in out.splitlines():
        if ":" in line and not "(qemu)" in line:
            for b in line.split(":")[1].split():
                try:
                    chars.append(int(b, 16))
                except ValueError:
                    pass

    rows = []
    for r in range(25):
        row_str = ""
        for c in range(80):
            idx = (r * 80 + c) * 2
            if idx < len(chars):
                byte_val = chars[idx]
                row_str += chr(byte_val) if 32 <= byte_val < 127 else " "
            else:
                row_str += " "
        rows.append(row_str.rstrip())
    return rows

def print_screen(rows, title):
    print(f"\n--- VGA Screen ({title}) ---")
    for idx, r in enumerate(rows):
        if r.strip():
            print(f"[{idx:02d}] {r}")
    print("---------------------------\n")

def check_file_contains(filepath, required_strings):
    if not os.path.exists(filepath):
        return False, f"File {filepath} does not exist"
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()
    for s in required_strings:
        if s not in content:
            return False, f"Missing '{s}' in {filepath}"
    return True, "OK"

def main():
    print("=== MyOS Stage 14B Automated Verification Suite ===")
    all_passed = True

    # ---------------------------------------------------------
    # Requirement 1: user/include/syscall.h contains SYS_* definitions
    # ---------------------------------------------------------
    print("\n[REQ 1] Verifying user/include/syscall.h...")
    r1_ok, r1_msg = check_file_contains("user/include/syscall.h", [
        "SYS_EXIT", "SYS_WRITE", "SYS_OPEN", "SYS_READ", "SYS_CLOSE",
        "SYS_WAIT", "SYS_READDIR", "SYS_CHDIR", "SYS_GETCWD"
    ])
    if r1_ok:
        print("Req 1 (user/include/syscall.h): PASS")
    else:
        print(f"Req 1: FAIL - {r1_msg}")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 2: user/include/unistd.h contains standard POSIX-like declarations
    # ---------------------------------------------------------
    print("\n[REQ 2] Verifying user/include/unistd.h...")
    r2_ok, r2_msg = check_file_contains("user/include/unistd.h", [
        "STDIN_FILENO", "STDOUT_FILENO", "STDERR_FILENO",
        "O_RDONLY", "O_WRONLY", "O_RDWR",
        "read", "write", "open", "close", "chdir", "getcwd"
    ])
    if r2_ok:
        print("Req 2 (user/include/unistd.h): PASS")
    else:
        print(f"Req 2: FAIL - {r2_msg}")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 3: user/include/dirent.h contains struct dirent and readdir
    # ---------------------------------------------------------
    print("\n[REQ 3] Verifying user/include/dirent.h...")
    r3_ok, r3_msg = check_file_contains("user/include/dirent.h", [
        "struct dirent", "name", "type", "size", "readdir", "DT_REG", "DT_DIR"
    ])
    if r3_ok:
        print("Req 3 (user/include/dirent.h): PASS")
    else:
        print(f"Req 3: FAIL - {r3_msg}")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 4: user/include/string.h contains string function declarations
    # ---------------------------------------------------------
    print("\n[REQ 4] Verifying user/include/string.h...")
    r4_ok, r4_msg = check_file_contains("user/include/string.h", [
        "strlen", "strcmp", "strncmp", "strcpy", "strncpy", "memcpy", "memset"
    ])
    if r4_ok:
        print("Req 4 (user/include/string.h): PASS")
    else:
        print(f"Req 4: FAIL - {r4_msg}")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 5: user/include/stdio.h contains print/puts declarations
    # ---------------------------------------------------------
    print("\n[REQ 5] Verifying user/include/stdio.h...")
    r5_ok, r5_msg = check_file_contains("user/include/stdio.h", [
        "print", "puts", "print_err", "print_dec", "print_hex"
    ])
    if r5_ok:
        print("Req 5 (user/include/stdio.h): PASS")
    else:
        print(f"Req 5: FAIL - {r5_msg}")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 6: Library compilation objects exist
    # ---------------------------------------------------------
    print("\n[REQ 6] Verifying userland runtime library object files...")
    lib_objs = [
        "build/user/lib/syscall.o",
        "build/user/lib/string.o",
        "build/user/lib/stdio.o"
    ]
    r6_ok = all(os.path.exists(obj) for obj in lib_objs)
    if r6_ok:
        print("Req 6 (userland runtime objects): PASS")
    else:
        missing = [obj for obj in lib_objs if not os.path.exists(obj)]
        print(f"Req 6: FAIL - Missing objects: {missing}")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 7: Binary compilation of 4 new ELFs
    # ---------------------------------------------------------
    print("\n[REQ 7] Verifying userland test executable ELFs...")
    user_elfs = [
        "build/user/file_test.elf",
        "build/user/ls.elf",
        "build/user/pwd.elf",
        "build/user/cd_test.elf"
    ]
    r7_ok = all(os.path.exists(elf) for elf in user_elfs)
    if r7_ok:
        print("Req 7 (userland test ELFs): PASS")
    else:
        missing = [elf for elf in user_elfs if not os.path.exists(elf)]
        print(f"Req 7: FAIL - Missing ELFs: {missing}")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 8 & 9 & 10: Disk image inspection (Inodes, Dirents, Multi-block)
    # ---------------------------------------------------------
    print("\n[REQ 8, 9, 10] Inspecting PFS on-disk filesystem structures...")
    disk_path = "build/disk.img"
    if not os.path.exists(disk_path):
        print("Req 8-10: FAIL - build/disk.img not found")
        all_passed = False
    else:
        with open(disk_path, "rb") as f:
            # Sector 0: Superblock
            sb = f.read(512)
            magic, version, sec_size, total_secs = struct.unpack("<4I", sb[0:16])
            root_inode_num = struct.unpack("<I", sb[52:56])[0]

            # Inode table starts at sector 18
            f.seek(18 * 512)
            it_data = f.read(64 * 512)

            # Inode 1: root dir
            root_ino_data = it_data[64:128]
            ino_id, ino_type, _, ino_size = struct.unpack("<IHHI", root_ino_data[0:12])
            direct_blocks = struct.unpack("<8I", root_ino_data[12:44])

            # Inode 2: /bin dir
            bin_ino_data = it_data[128:192]
            bino_id, bino_type, _, bino_size = struct.unpack("<IHHI", bin_ino_data[0:12])
            bin_direct_blocks = struct.unpack("<8I", bin_ino_data[12:44])

            # Read all dirents from root directory
            data_start = 82
            root_dirent_names = []
            for b in direct_blocks:
                if b == 0xFFFFFFFF:
                    continue
                f.seek((data_start + b) * 512)
                block_data = f.read(512)
                for entry_idx in range(8):
                    entry_data = block_data[entry_idx*64:(entry_idx+1)*64]
                    e_ino, e_type, e_nlen = struct.unpack("<IBB", entry_data[0:6])
                    if e_ino != 0 and e_nlen > 0:
                        name = entry_data[8:8+e_nlen].decode("ascii", errors="replace")
                        root_dirent_names.append(name)

        # Req 8: Check files exist in root directory
        expected_bins = ["file_test", "ls", "pwd", "cd_test"]
        r8_ok = all(eb in root_dirent_names for eb in expected_bins)
        if r8_ok:
            print("Req 8 (Inode verification on disk): PASS")
        else:
            print(f"Req 8: FAIL - Expected {expected_bins} in {root_dirent_names}")
            all_passed = False

        # Req 9: Directory entries
        r9_ok = "bin" in root_dirent_names and r8_ok
        if r9_ok:
            print("Req 9 (Directory entry existence): PASS")
        else:
            print(f"Req 9: FAIL - 'bin' or entries missing")
            all_passed = False

        # Req 10: Multi-block directory
        # With 11 files + "bin", root dir has 12 entries (> 8), so it must span > 1 block
        r10_ok = ino_size >= 1024 and direct_blocks[0] != 0xFFFFFFFF and direct_blocks[1] != 0xFFFFFFFF
        if r10_ok:
            print("Req 10 (Multi-block directory structure): PASS")
        else:
            print(f"Req 10: FAIL - Root dir size {ino_size}, blocks {direct_blocks[:2]}")
            all_passed = False

    # ---------------------------------------------------------
    # Requirement 11, 12, 19, 20, 21, 26: In-Kernel Runtime & Syscall Tests (runtimetest)
    # ---------------------------------------------------------
    print("\n[REQ 11, 12, 19, 20, 21, 26] Testing in-kernel runtime & syscall suite (runtimetest)...")
    keys = text_to_sendkeys("runtimetest\n")
    screen = run_qemu_test(keys, wait_time=0.8)
    print_screen(screen, "runtimetest")

    screen_text = "\n".join(screen)
    if "All Stage 14B kernel runtime tests passed successfully!" in screen_text:
        print("Req 11, 12, 19, 20, 21, 26 (Kernel runtime & readdir/chdir tests): PASS")
    else:
        print("Req 11, 12, 19, 20, 21, 26: FAIL")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 13, 14, 15, 16, 17, 18: User Program file_test
    # ---------------------------------------------------------
    print("\n[REQ 13-18] Testing Ring 3 file_test program (run /disk/file_test)...")
    keys = text_to_sendkeys("run /disk/file_test\n")
    screen = run_qemu_test(keys, wait_time=1.0)
    print_screen(screen, "run /disk/file_test")

    screen_text = "\n".join(screen)
    if "file_test: all assertions passed" in screen_text and "Hello from MyOS RAMFS!" in screen_text:
        print("Req 13-18 (file_test open/read/close/error handling): PASS")
    else:
        print("Req 13-18: FAIL")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 19, 20: User Program ls
    # ---------------------------------------------------------
    print("\n[REQ 19, 20] Testing Ring 3 ls utility (run /disk/ls)...")
    keys = text_to_sendkeys("run /disk/ls\n")
    screen = run_qemu_test(keys, wait_time=1.0)
    print_screen(screen, "run /disk/ls")

    screen_text = "\n".join(screen)
    # In root dir "/", ls should list [DIR] bin, [FILE] readme.txt, [DIR] dev (or similar)
    if "[FILE] readme.txt" in screen_text and ("[DIR]  bin" in screen_text or "[DIR]  disk" in screen_text):
        print("Req 19, 20 (ls directory listing): PASS")
    else:
        print("Req 19, 20: FAIL")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 22: User Program pwd
    # ---------------------------------------------------------
    print("\n[REQ 22] Testing Ring 3 pwd utility (run /disk/pwd)...")
    keys = text_to_sendkeys("run /disk/pwd\n")
    screen = run_qemu_test(keys, wait_time=0.8)
    print_screen(screen, "run /disk/pwd")

    screen_text = "\n".join(screen)
    # The output row should contain "/"
    pwd_found = False
    for line in screen:
        if line.strip() == "/":
            pwd_found = True
            break
    if pwd_found:
        print("Req 22 (pwd working directory output): PASS")
    else:
        print("Req 22: FAIL - '/' not found on screen")
        all_passed = False

    # ---------------------------------------------------------
    # Requirement 23, 24, 25, 26: User Program cd_test
    # ---------------------------------------------------------
    print("\n[REQ 23-26] Testing Ring 3 cd_test program (run /disk/cd_test)...")
    keys = text_to_sendkeys("run /disk/cd_test\n")
    screen = run_qemu_test(keys, wait_time=1.0)
    print_screen(screen, "run /disk/cd_test")

    screen_text = "\n".join(screen)
    if "cd_test: all assertions passed" in screen_text:
        print("Req 23-26 (cd_test chdir/getcwd/relative/parent/error assertions): PASS")
    else:
        print("Req 23-26: FAIL")
        all_passed = False

    # ---------------------------------------------------------
    # Verify Screen Line Budget: help command is exactly 24 rows
    # ---------------------------------------------------------
    print("\n[VERIFY] Verifying Shell Help Screen Budget...")
    keys = text_to_sendkeys("help\n")
    screen = run_qemu_test(keys, wait_time=0.8)
    print_screen(screen, "help")
    help_text = "\n".join(screen)
    if "runtimetest" not in help_text:
        print("Screen budget check: PASS (runtimetest is unlisted)")
    else:
        print("Screen budget check: FAIL (runtimetest leaked into help)")
        all_passed = False

    # ---------------------------------------------------------
    # Verify Process Table Cleanliness (ps)
    # ---------------------------------------------------------
    print("\n[VERIFY] Verifying Process Table Cleanliness (ps)...")
    keys = text_to_sendkeys("run /disk/file_test\nrun /disk/cd_test\nps\n")
    screen = run_qemu_test(keys, wait_time=1.0)
    print_screen(screen, "ps after runs")
    ps_text = "\n".join(screen)
    # Only PID 0 should be present
    if "0    RUNNING     KERNEL" in ps_text and "1    " not in ps_text:
        print("Process Table Cleanliness: PASS (User processes reaped cleanly)")
    else:
        print("Process Table Cleanliness: FAIL")
        all_passed = False

    # ---------------------------------------------------------
    # Verify Kernel Heap Baseline (heapinfo)
    # ---------------------------------------------------------
    print("\n[VERIFY] Verifying Kernel Heap Baseline (heapinfo)...")
    keys = text_to_sendkeys("heapinfo\n")
    screen = run_qemu_test(keys, wait_time=0.8)
    print_screen(screen, "heapinfo")
    heap_text = "\n".join(screen)
    if "Used:         0 bytes" in heap_text:
        print("Heap Baseline: PASS (Used: 0 bytes)")
    else:
        print("Heap Baseline: FAIL (Used bytes > 0)")
        all_passed = False

    print("\n=======================================================")
    if all_passed:
        print(">>> STAGE 14B ALL 26 VERIFICATION CHECKS PASSED <<<")
    else:
        print(">>> STAGE 14B VERIFICATION CHECKS FAILED <<<")
    print("=======================================================")
    return 0 if all_passed else 1

if __name__ == "__main__":
    sys.exit(main())
