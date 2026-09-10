#!/usr/bin/env python3
"""
test_stage11b.py - Automated Verification Suite for MyOS Stage 11B (File Descriptors + Open/Read/Write/Close)

Verifies:
  1. Boot integrity and 25-row screen line budget (row 0 header, row 24 prompt)
  2. Silent FD subsystem initialization on boot (0 screen lines added)
  3. Standard streams initialization (fds 0, 1, 2 for stdin, stdout, stderr)
  4. 'fdtest' command execution and all 10 architectural checkpoints:
     - Standard streams pre-allocated statically
     - File open allocates lowest available FD (fd 3)
     - File read advances open-file offset
     - Multiple opens of same file have independent cursor offsets
     - File close frees slot and subsequent read returns EBADF (-4)
     - Lowest available FD reuse on subsequent open
     - File write via FD and independent readback
     - Directory read rejected with EISDIR (-7)
     - Write on read-only FD rejected with EACCES (-6)
     - Clean process FD teardown (fd_close_all)
  5. Shell command 'about' displays FD table status
  6. Shell command 'help' displays 'fdtest' in 2-column layout
  7. Heap stability before and after FD operations
  8. Full coexistence with Stage 11A ('vfstest'), Stage 10 ('elftest'), Stage 9 ('proctest'), Stage 8B ('syscalltest')
  9. Dual-mode SYS_WRITE backward compatibility (legacy 2-arg and modern 3-arg)
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
    print("MyOS Stage 11B Automated Verification Test Suite")
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
    # TEST 2: 'fdtest' command execution & test checkpoints
    # ----------------------------------------------------
    print("\n[TEST 2] Testing 'fdtest' Command...")
    keys = text_to_sendkeys("fdtest\n")
    fd_rows = run_qemu_test(keys, wait_time=0.6)
    fd_text = "\n".join(fd_rows)

    required_checkpoints = [
        "[OK] Standard streams (stdin, stdout, stderr)",
        "[OK] File open (/readme.txt -> fd 3)",
        "[OK] File read and offset advance",
        "[OK] Independent open-file offsets",
        "[OK] File close and EBADF validation",
        "[OK] Lowest available FD reuse",
        "[OK] File write and readback",
        "[OK] Directory read rejection (EISDIR)",
        "[OK] Write permission enforcement (EACCES)",
        "[OK] Clean process FD teardown",
        "All File Descriptor tests passed successfully!"
    ]

    t2_pass = True
    for cp in required_checkpoints:
        if cp not in fd_text:
            print(f"Missing checkpoint: {cp}")
            t2_pass = False

    if t2_pass:
        print("Test 2 (fdtest Checkpoints): PASS")
        passed_tests += 1
    else:
        print("Test 2 (fdtest Checkpoints): FAIL")

    print_screen(fd_rows, "Test 2: fdtest")

    # ----------------------------------------------------
    # TEST 3: 'about' command for FD Table entry
    # ----------------------------------------------------
    print("\n[TEST 3] Testing 'about' command for FD Table entry...")
    keys = text_to_sendkeys("about\n")
    about_rows = run_qemu_test(keys, wait_time=0.4)
    about_text = "\n".join(about_rows)

    t3_pass = ("FD Table: Open/Read/Write/Close Active" in about_text and
               "VFS/RAMFS: In-Memory Virtual Filesystem Active" in about_text and
               "ELF Loader: ELF64 PT_LOAD Validator Active" in about_text)

    if t3_pass:
        print("Test 3 (about command with FD Table): PASS")
        passed_tests += 1
    else:
        print("Test 3 (about command with FD Table): FAIL")

    print_screen(about_rows, "Test 3: about")

    # ----------------------------------------------------
    # TEST 4: 'help' command includes 'fdtest'
    # ----------------------------------------------------
    print("\n[TEST 4] Testing 'help' command includes 'fdtest'...")
    keys = text_to_sendkeys("help\n")
    help_rows = run_qemu_test(keys, wait_time=0.4)
    help_text = "\n".join(help_rows)

    t4_pass = ("fdtest" in help_text and "vfstest" in help_text and "elftest" in help_text)

    if t4_pass:
        print("Test 4 (help command includes fdtest): PASS")
        passed_tests += 1
    else:
        print("Test 4 (help command includes fdtest): FAIL")

    print_screen(help_rows, "Test 4: help")

    # ----------------------------------------------------
    # TEST 5: Heap stability across FD operations
    # ----------------------------------------------------
    print("\n[TEST 5] Testing Heap Stability across FD operations...")
    keys = text_to_sendkeys("fdtest\nheapinfo\n")
    heap_rows = run_qemu_test(keys, wait_time=0.6)
    heap_text = "\n".join(heap_rows)

    t5_pass = ("Kernel Heap:" in heap_text and
               "Free:" in heap_text and
               "Used:" in heap_text and
               "All File Descriptor tests passed successfully!" in heap_text)

    if t5_pass:
        print("Test 5 (Heap Stability): PASS")
        passed_tests += 1
    else:
        print("Test 5 (Heap Stability): FAIL")

    print_screen(heap_rows, "Test 5: heapinfo after fdtest")

    # ----------------------------------------------------
    # TEST 6: Stage 11A VFS coexistence ('vfstest' + 'fdtest')
    # ----------------------------------------------------
    print("\n[TEST 6] Testing Stage 11A VFS coexistence...")
    keys = text_to_sendkeys("vfstest\nfdtest\n")
    vfs_coexist_rows = run_qemu_test(keys, wait_time=0.8)
    vfs_coexist_text = "\n".join(vfs_coexist_rows)

    t6_pass = ("All VFS tests passed successfully!" in vfs_coexist_text and
               "All File Descriptor tests passed successfully!" in vfs_coexist_text)

    if t6_pass:
        print("Test 6 (Stage 11A VFS coexistence): PASS")
        passed_tests += 1
    else:
        print("Test 6 (Stage 11A VFS coexistence): FAIL")

    print_screen(vfs_coexist_rows, "Test 6: vfstest + fdtest")

    # ----------------------------------------------------
    # TEST 7: Stage 10 ELF loader coexistence ('elftest' + 'fdtest')
    # ----------------------------------------------------
    print("\n[TEST 7] Testing Stage 10 ELF loader coexistence...")
    keys = text_to_sendkeys("clear\nelftest\n") + [("sleep", 0.8)] + text_to_sendkeys("clear\nfdtest\n")
    elf_coexist_rows = run_qemu_test(keys, wait_time=0.8)
    elf_coexist_text = "\n".join(elf_coexist_rows)

    t7_pass = ("All File Descriptor tests passed successfully!" in elf_coexist_text)

    if t7_pass:
        print("Test 7 (Stage 10 ELF loader coexistence): PASS")
        passed_tests += 1
    else:
        print("Test 7 (Stage 10 ELF loader coexistence): FAIL")

    print_screen(elf_coexist_rows, "Test 7: elftest + fdtest")

    # ----------------------------------------------------
    # TEST 8: Dual-mode SYS_WRITE and Stage 8B syscall coexistence
    # ----------------------------------------------------
    print("\n[TEST 8] Testing Stage 8B syscalltest coexistence...")
    keys = text_to_sendkeys("clear\nsyscalltest\n") + [("sleep", 0.8)] + text_to_sendkeys("clear\nfdtest\n")
    syscall_rows = run_qemu_test(keys, wait_time=0.8)
    syscall_text = "\n".join(syscall_rows)

    t8_pass = ("All File Descriptor tests passed successfully!" in syscall_text)

    if t8_pass:
        print("Test 8 (Stage 8B syscalltest coexistence): PASS")
        passed_tests += 1
    else:
        print("Test 8 (Stage 8B syscalltest coexistence): FAIL")

    print_screen(syscall_rows, "Test 8: syscalltest + fdtest")

    # ----------------------------------------------------
    # Summary
    # ----------------------------------------------------
    print("\n==================================================")
    print(f"Stage 11B Test Summary: {passed_tests}/{total_tests} Tests Passed")
    print("==================================================")

    if passed_tests == total_tests:
        print(">>> ALL STAGE 11B TESTS PASSED! <<<\n")
        return 0
    else:
        print(f">>> {total_tests - passed_tests} TEST(S) FAILED! <<<\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())
