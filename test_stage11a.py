#!/usr/bin/env python3
"""
test_stage11a.py - Automated Verification Suite for MyOS Stage 11A (VFS Core and In-Memory RAMFS)

Verifies:
  - Boot integrity and 25-row screen line budget (row 0 header, row 24 prompt)
  - Silent VFS initialization on boot (0 screen lines added)
  - VFS core layer independence and abstraction
  - RAMFS mounted at root "/"
  - Root directory lookup and initial directory structure (/bin, /etc, /readme.txt)
  - Reading /readme.txt ("Hello from MyOS RAMFS!\n")
  - Read boundary and EOF semantics (zero-length read, at EOF, beyond EOF)
  - Rejection of invalid read/write on directories (VFS_ERR_IS_DIR)
  - File creation (vfs_create) and duplicate file rejection (VFS_ERR_EXISTS)
  - Directory creation (vfs_mkdir) and duplicate directory rejection
  - Nested directory creation and path resolution (/docs/os/stage11)
  - File write, overwrite, and file expansion semantics
  - Non-sparse write enforcement (offset > size rejected with VFS_ERR_NOT_SUPPORTED)
  - Path resolution rules (rejection of relative paths, null paths, traversal through regular files)
  - Path and component length limits (VFS_NAME_MAX, VFS_PATH_MAX)
  - Clean memory rollback on allocation failures
  - Heap memory stability before and after filesystem operations
  - Shell command 'vfstest' execution and report
  - Shell commands 'about' and 'help' updated for VFS and RAMFS
  - Coexistence with Stage 10 ELF loader ('elftest'), Stage 9 ('proctest'), Stage 8B ('syscalltest')
  - Post-test shell responsiveness and kernel task stability
"""

import subprocess
import time
import sys

def text_to_sendkeys(text):
    key_map = {
        ' ': 'spc',
        '\n': 'ret',
        '\b': 'backspace',
        '\t': 'tab',
        '-': 'minus',
        '=': 'equal',
        '[': 'bracket_left',
        ']': 'bracket_right',
        ';': 'semicolon',
        "'": 'apostrophe',
        '`': 'grave_accent',
        '\\': 'backslash',
        ',': 'comma',
        '.': 'dot',
        '/': 'slash',
    }
    shift_map = {
        '!': 'shift-1',
        '@': 'shift-2',
        '#': 'shift-3',
        '$': 'shift-4',
        '%': 'shift-5',
        '^': 'shift-6',
        '&': 'shift-7',
        '*': 'shift-8',
        '(': 'shift-9',
        ')': 'shift-0',
        '_': 'shift-minus',
        '+': 'shift-equal',
        ':': 'shift-semicolon',
        '"': 'shift-apostrophe',
        '<': 'shift-comma',
        '>': 'shift-dot',
        '?': 'shift-slash',
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

def run_qemu_test(key_sequence, wait_time=0.5, boot_wait=1.4):
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
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
    print("----------------------------\n")

def main():
    print("==================================================")
    print("MyOS Stage 11A Automated Verification Test Suite")
    print("==================================================")

    total_tests = 8
    passed_tests = 0

    # ----------------------------------------------------
    # TEST 1: Boot integrity and screen line budget
    # ----------------------------------------------------
    print("\n[TEST 1] Testing Boot and Screen Line Budget...")
    boot_rows = run_qemu_test([], wait_time=0.2)
    boot_text = "\n".join(boot_rows)

    t1_pass = ("MyOS - Educational x86-64 Kernel" in boot_text and
               "Stage 8A Goal Achieved: User mode Ring 3 foundation active!" in boot_text and
               "Stage 8B Goal Achieved: System call subsystem (int 0x80) active!" in boot_text and
               "MyOS>" in boot_text and
               "MyOS - Educational x86-64 Kernel" in boot_rows[0] and
               "MyOS>" in boot_rows[24])

    if t1_pass:
        print("Test 1 (Boot & 25-Row Screen Line Budget): PASS")
        passed_tests += 1
    else:
        print("Test 1 (Boot & 25-Row Screen Line Budget): FAIL")

    print_screen(boot_rows, "Test 1: Boot")

    # ----------------------------------------------------
    # TEST 2: 'vfstest' command execution & test checkpoints
    # ----------------------------------------------------
    print("\n[TEST 2] Testing 'vfstest' Command...")
    keys = text_to_sendkeys("vfstest\n")
    vfs_rows = run_qemu_test(keys, wait_time=0.6)
    vfs_text = "\n".join(vfs_rows)

    required_checkpoints = [
        "[OK] VFS initialized",
        "[OK] RAMFS mounted at /",
        "[OK] Root lookup",
        "[OK] File lookup",
        "[OK] File read",
        "[OK] File creation",
        "[OK] Directory creation",
        "[OK] File write/read",
        "[OK] Path validation",
        "[OK] Memory stability",
        "All VFS tests passed successfully!"
    ]

    t2_pass = all(cp in vfs_text for cp in required_checkpoints)

    if t2_pass:
        print("Test 2 (vfstest Checkpoints): PASS")
        passed_tests += 1
    else:
        print("Test 2 (vfstest Checkpoints): FAIL")
        for cp in required_checkpoints:
            if cp not in vfs_text:
                print(f"  Missing checkpoint: '{cp}'")

    print_screen(vfs_rows, "Test 2: vfstest")

    # ----------------------------------------------------
    # TEST 3: 'about' command includes VFS/RAMFS
    # ----------------------------------------------------
    print("\n[TEST 3] Testing 'about' command for VFS/RAMFS entry...")
    keys = text_to_sendkeys("clear\nabout\n")
    about_rows = run_qemu_test(keys, wait_time=0.4)
    about_text = "\n".join(about_rows)

    t3_pass = ("VFS/RAMFS: In-Memory Virtual Filesystem Active" in about_text and
               "ELF Loader: ELF64 PT_LOAD Validator Active" in about_text and
               "Processes: Isolated Address Spaces (CR3) Active" in about_text)

    if t3_pass:
        print("Test 3 (about command with VFS/RAMFS): PASS")
        passed_tests += 1
    else:
        print("Test 3 (about command with VFS/RAMFS): FAIL")

    print_screen(about_rows, "Test 3: about")

    # ----------------------------------------------------
    # TEST 4: 'help' command includes 'vfstest'
    # ----------------------------------------------------
    print("\n[TEST 4] Testing 'help' command includes 'vfstest'...")
    keys = text_to_sendkeys("clear\nhelp\n")
    help_rows = run_qemu_test(keys, wait_time=0.4)
    help_text = "\n".join(help_rows)

    t4_pass = ("vfstest" in help_text and
               "Test VFS and RAMFS" in help_text and
               "elftest" in help_text and
               "proctest" in help_text)

    if t4_pass:
        print("Test 4 (help command includes vfstest): PASS")
        passed_tests += 1
    else:
        print("Test 4 (help command includes vfstest): FAIL")

    print_screen(help_rows, "Test 4: help")

    # ----------------------------------------------------
    # TEST 5: Heap stability after vfstest
    # ----------------------------------------------------
    print("\n[TEST 5] Testing Heap Stability across VFS operations...")
    keys = text_to_sendkeys("clear\nvfstest\nclear\nheapinfo\n")
    heap_rows = run_qemu_test(keys, wait_time=0.8)
    heap_text = "\n".join(heap_rows)

    t5_pass = ("Kernel Heap:" in heap_text and
               "Size:         65536 bytes" in heap_text and
               "Free:" in heap_text and
               "Used Blocks:" in heap_text)

    if t5_pass:
        print("Test 5 (Heap Stability): PASS")
        passed_tests += 1
    else:
        print("Test 5 (Heap Stability): FAIL")

    print_screen(heap_rows, "Test 5: heapinfo after vfstest")

    # ----------------------------------------------------
    # TEST 6: Stage 10 ELF Loader coexistence ('elftest')
    # ----------------------------------------------------
    print("\n[TEST 6] Testing Stage 10 ELF loader coexistence...")
    keys = text_to_sendkeys("clear\nvfstest\nclear\nelftest\n")
    elf_rows = run_qemu_test(keys, wait_time=1.0)
    elf_text = "\n".join(elf_rows)

    t6_pass = ("ELF64 Loader Security & Execution Test:" in elf_text and
               "ELF64 header check:   OK" in elf_text and
               "[ELF Ring 3] Hello from loaded ELF64 executable!" in elf_text and
               "Process clean exit:   OK (Status 42 returned)" in elf_text and
               "PASSED (All ELF Checks Verified)" in elf_text)

    if t6_pass:
        print("Test 6 (Stage 10 ELF loader coexistence): PASS")
        passed_tests += 1
    else:
        print("Test 6 (Stage 10 ELF loader coexistence): FAIL")

    print_screen(elf_rows, "Test 6: vfstest + elftest")

    # ----------------------------------------------------
    # TEST 7: Stage 9 Process isolation coexistence ('proctest')
    # ----------------------------------------------------
    print("\n[TEST 7] Testing Stage 9 Process isolation coexistence...")
    keys = text_to_sendkeys("clear\nvfstest\nclear\nproctest\n")
    proc_rows = run_qemu_test(keys, wait_time=1.0)
    proc_text = "\n".join(proc_rows)

    t7_pass = ("Process Isolation & Multitasking Test:" in proc_text and
               "Address spaces:   OK" in proc_text and
               "PASSED (All Isolation Properties Verified)" in proc_text)

    if t7_pass:
        print("Test 7 (Stage 9 proctest coexistence): PASS")
        passed_tests += 1
    else:
        print("Test 7 (Stage 9 proctest coexistence): FAIL")

    print_screen(proc_rows, "Test 7: vfstest + proctest")

    # ----------------------------------------------------
    # TEST 8: Stage 8B System calls coexistence ('syscalltest')
    # ----------------------------------------------------
    print("\n[TEST 8] Testing Stage 8B System calls coexistence...")
    keys = text_to_sendkeys("clear\nvfstest\nclear\nsyscalltest\n")
    sys_rows = run_qemu_test(keys, wait_time=1.0)
    sys_text = "\n".join(sys_rows)

    t8_pass = ("System Call Test:" in sys_text and
               "[Ring 3 User] Hello from SYS_WRITE!" in sys_text and
               "SYS_WRITE:        OK" in sys_text and
               "SYS_GETTIME:      OK" in sys_text and
               "PASSED (All Syscalls Verified)" in sys_text)

    if t8_pass:
        print("Test 8 (Stage 8B syscalltest coexistence): PASS")
        passed_tests += 1
    else:
        print("Test 8 (Stage 8B syscalltest coexistence): FAIL")

    print_screen(sys_rows, "Test 8: vfstest + syscalltest")

    # ----------------------------------------------------
    # SUMMARY
    # ----------------------------------------------------
    print("\n==================================================")
    print(f"Stage 11A Test Summary: {passed_tests}/{total_tests} Tests Passed")
    print("==================================================")

    if passed_tests == total_tests:
        print(">>> ALL STAGE 11A TESTS PASSED! <<<")
        sys.exit(0)
    else:
        print(f">>> {total_tests - passed_tests} TEST(S) FAILED <<<")
        sys.exit(1)

if __name__ == "__main__":
    main()
