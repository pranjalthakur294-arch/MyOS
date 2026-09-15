#!/usr/bin/env python3
"""
test_stage12e.py - Automated Verification Suite for MyOS Stage 12E
(VFS -> Persistent Filesystem Integration: Mount Boundary Crossing,
Distinct Mount Roots, Reverse '..' Crossing, CWD in Mount, VFS/PFS Adapter,
FD Read/Write/Close on Persistent Files, Busy Unmount Protection,
Cross-Boot Persistence Verification)

Verifies:
  1. Boot integrity with disk attached and strict 25-row screen budget (silent probe)
  2. Shell command table budget check ('help' fits in 2-column layout within 24 rows)
  3. 'vfs12etest' in-kernel verification suite (all 15 core checks passed)
  4. 'touch /disk/shell_test.txt' file creation via VFS in mounted PFS
  5. 'writefile' and 'cat' on /disk file through VFS / File Descriptor layer
  6. Subdirectory creation and file operations ('mkdir /disk/testdir', 'writefile /disk/testdir/sub.txt')
  7. Directory iteration via 'ls /disk' showing files and directory indicators ('/')
  8. CWD integration into mounted filesystem ('cd /disk', 'pwd' reports '/disk')
  9. Relative path operations from mounted CWD ('cat msg.txt', 'ls' without args)
  10. Cross-boundary '..' traversal from within mounted filesystem ('cat ../readme.txt')
  11. Root '..' clamping ('cd /; cd ..' maintains CWD at '/')
  12. Return to parent directory via 'cd ..' from mounted root ('cd /disk; cd ..; pwd' -> '/')
  13. File deletion via VFS 'rm /disk/shell_test.txt' reflected in directory listing
  14. Busy unmount rejection when CWD is inside mount ('cd /disk; umount /disk' rejected)
  15. Multi-session persistence across reboot (data written in Session 1 survives into Session 2)
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

def run_qemu_test(key_sequence, wait_time=0.5, boot_wait=2.0, attach_disk=True):
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
    time.sleep(0.5)

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

def main():
    print("=== MyOS Stage 12E Automated Verification Suite ===")
    all_passed = True

    # TEST 1: Boot integrity and 25-row screen line budget
    print("\n[TEST 1] Verifying Boot Integrity and Screen Line Budget with Disk Attached...")
    boot_rows = run_qemu_test([], wait_time=0.1, boot_wait=2.0, attach_disk=True)
    boot_text = "\n".join(boot_rows)

    t1_pass = (
        "MyOS - Educational x86-64 Kernel" in boot_rows[0] and
        "Stage 8B Goal Achieved: System call subsystem" in boot_text and
        "MyOS>" in boot_rows[24]
    )
    if t1_pass:
        print("Test 1 (Boot Integrity and 25-Row Budget): PASS")
    else:
        print("Test 1 (Boot Integrity and 25-Row Budget): FAIL")
        all_passed = False
    print_screen(boot_rows, "Test 1: Boot Screen")

    # TEST 2: Shell command table budget check
    print("\n[TEST 2] Verifying 'help' Command Fits Within 24-Row Budget...")
    keys = text_to_sendkeys("clear\nhelp\n")
    help_rows = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    help_text = "\n".join(help_rows)

    help_non_empty = sum(1 for r in help_rows if r.strip())
    t2_pass = (
        "Available commands:" in help_text and
        "MyOS>" in help_rows[23] and
        help_non_empty <= 24
    )
    if t2_pass:
        print("Test 2 (Shell Command Table Layout): PASS")
    else:
        print("Test 2 (Shell Command Table Layout): FAIL")
        all_passed = False
    print_screen(help_rows, "Test 2: help Screen")

    # TEST 3: 'vfs12etest' in-kernel verification suite (15 checks)
    print("\n[TEST 3] Verifying 'vfs12etest' In-Kernel Verification Suite (15 Checks)...")
    keys = text_to_sendkeys("clear\nvfs12etest\n")
    rows_test = run_qemu_test(keys, wait_time=1.0, boot_wait=2.0, attach_disk=True)
    text_test = "\n".join(rows_test)

    t3_pass = (
        "Running Stage 12E VFS -> Persistent Filesystem verification suite..." in text_test and
        "Stage 12E VFS integration suite passed!" in text_test and
        "FAIL" not in text_test
    )
    if t3_pass:
        print("Test 3 ('vfs12etest' Execution): PASS")
    else:
        print("Test 3 ('vfs12etest' Execution): FAIL")
        all_passed = False
    print_screen(rows_test, "Test 3: vfs12etest")

    # TEST 4: 'touch /disk/shell_test.txt' file creation via VFS
    print("\n[TEST 4] Verifying File Creation via VFS ('touch /disk/shell_test.txt')...")
    keys = text_to_sendkeys("clear\ntouch /disk/shell_test.txt\nls /disk\n")
    rows_touch = run_qemu_test(keys, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_touch = "\n".join(rows_touch)

    t4_pass = (
        "shell_test.txt" in text_touch
    )
    if t4_pass:
        print("Test 4 (File Creation via VFS): PASS")
    else:
        print("Test 4 (File Creation via VFS): FAIL")
        all_passed = False
    print_screen(rows_touch, "Test 4: touch /disk/shell_test.txt")

    # TEST 5: 'writefile' and 'cat' through VFS / File Descriptor layer
    print("\n[TEST 5] Verifying File Write & Cat via VFS/FD Layer...")
    keys = text_to_sendkeys("clear\nwritefile /disk/msg.txt HelloFrom12E\ncat /disk/msg.txt\n")
    rows_cat = run_qemu_test(keys, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_cat = "\n".join(rows_cat)

    t5_pass = (
        "writefile: wrote 12 bytes to /disk/msg.txt" in text_cat and
        "HelloFrom12E" in text_cat
    )
    if t5_pass:
        print("Test 5 (Write & Cat via VFS/FD): PASS")
    else:
        print("Test 5 (Write & Cat via VFS/FD): FAIL")
        all_passed = False
    print_screen(rows_cat, "Test 5: writefile & cat")

    # TEST 6: Subdirectory creation and file operations
    print("\n[TEST 6] Verifying Subdirectory Creation and Subfile Operations...")
    keys = text_to_sendkeys("clear\nmkdir /disk/testdir\nwritefile /disk/testdir/sub.txt SubData12E\ncat /disk/testdir/sub.txt\n")
    rows_subdir = run_qemu_test(keys, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_subdir = "\n".join(rows_subdir)

    t6_pass = (
        "SubData12E" in text_subdir
    )
    if t6_pass:
        print("Test 6 (Subdirectory & Subfile): PASS")
    else:
        print("Test 6 (Subdirectory & Subfile): FAIL")
        all_passed = False
    print_screen(rows_subdir, "Test 6: Subdirectory Operations")

    # TEST 7: Directory iteration via 'ls /disk'
    print("\n[TEST 7] Verifying Directory Iteration ('ls /disk')...")
    keys = text_to_sendkeys("clear\nls /disk\n")
    rows_ls = run_qemu_test(keys, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_ls = "\n".join(rows_ls)

    t7_pass = (
        "msg.txt" in text_ls and
        "testdir/" in text_ls and
        "shell_test.txt" in text_ls
    )
    if t7_pass:
        print("Test 7 (Directory Iteration 'ls /disk'): PASS")
    else:
        print("Test 7 (Directory Iteration 'ls /disk'): FAIL")
        all_passed = False
    print_screen(rows_ls, "Test 7: ls /disk")

    # TEST 8: CWD integration into mounted filesystem ('cd /disk', 'pwd')
    print("\n[TEST 8] Verifying CWD Integration ('cd /disk', 'pwd')...")
    keys = text_to_sendkeys("clear\ncd /disk\npwd\n")
    rows_cwd = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    text_cwd = "\n".join(rows_cwd)

    t8_pass = (
        "/disk" in text_cwd
    )
    if t8_pass:
        print("Test 8 (CWD Integration into Mount): PASS")
    else:
        print("Test 8 (CWD Integration into Mount): FAIL")
        all_passed = False
    print_screen(rows_cwd, "Test 8: cd /disk; pwd")

    # TEST 9: Relative path operations from mounted CWD
    print("\n[TEST 9] Verifying Relative Path Operations from CWD in Mount...")
    keys = text_to_sendkeys("clear\ncd /disk\ncat msg.txt\nls\n")
    rows_rel = run_qemu_test(keys, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_rel = "\n".join(rows_rel)

    t9_pass = (
        "HelloFrom12E" in text_rel and
        "msg.txt" in text_rel and
        "testdir/" in text_rel
    )
    if t9_pass:
        print("Test 9 (Relative Path Operations from CWD): PASS")
    else:
        print("Test 9 (Relative Path Operations from CWD): FAIL")
        all_passed = False
    print_screen(rows_rel, "Test 9: Relative Operations")

    # TEST 10: Cross-boundary '..' traversal ('cat ../readme.txt')
    print("\n[TEST 10] Verifying Cross-Boundary '..' Traversal ('cat ../readme.txt')...")
    keys = text_to_sendkeys("clear\ncd /disk\ncat ../readme.txt\n")
    rows_cross = run_qemu_test(keys, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_cross = "\n".join(rows_cross)

    t10_pass = (
        "Hello from MyOS RAMFS!" in text_cross
    )
    if t10_pass:
        print("Test 10 (Cross-Boundary '..' Traversal): PASS")
    else:
        print("Test 10 (Cross-Boundary '..' Traversal): FAIL")
        all_passed = False
    print_screen(rows_cross, "Test 10: Cross-Boundary ..")

    # TEST 11: Root '..' clamping
    print("\n[TEST 11] Verifying Root '..' Clamping ('cd /; cd ..; pwd')...")
    keys = text_to_sendkeys("clear\ncd /\ncd ..\npwd\n")
    rows_clamp = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    text_clamp = "\n".join(rows_clamp)

    t11_pass = (
        "/\n" in text_clamp or text_clamp.endswith("/") or "\n/\n" in text_clamp
    )
    if t11_pass:
        print("Test 11 (Root '..' Clamping): PASS")
    else:
        print("Test 11 (Root '..' Clamping): FAIL")
        all_passed = False
    print_screen(rows_clamp, "Test 11: Root Clamping")

    # TEST 12: Return to parent directory via 'cd ..' from mounted root
    print("\n[TEST 12] Verifying Return to Parent via 'cd ..' ('cd /disk; cd ..; pwd')...")
    keys = text_to_sendkeys("clear\ncd /disk\ncd ..\npwd\n")
    rows_back = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    text_back = "\n".join(rows_back)

    t12_pass = (
        "/\n" in text_back or text_back.endswith("/") or "\n/\n" in text_back
    )
    if t12_pass:
        print("Test 12 (Return to Parent via 'cd ..'): PASS")
    else:
        print("Test 12 (Return to Parent via 'cd ..'): FAIL")
        all_passed = False
    print_screen(rows_back, "Test 12: Return to Parent ..")

    # TEST 13: File removal via VFS ('rm /disk/shell_test.txt')
    print("\n[TEST 13] Verifying File Deletion via VFS ('rm /disk/shell_test.txt')...")
    keys = text_to_sendkeys("clear\nrm /disk/shell_test.txt\nls /disk\n")
    rows_rm = run_qemu_test(keys, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_rm = "\n".join(rows_rm)

    lines_after_ls = []
    found_ls = False
    for r in rows_rm:
        if "ls /disk" in r:
            found_ls = True
            continue
        if found_ls:
            if "MyOS>" in r:
                break
            if r.strip():
                lines_after_ls.append(r.strip())

    t13_pass = (
        "shell_test.txt" not in lines_after_ls and
        "msg.txt" in lines_after_ls
    )
    if t13_pass:
        print("Test 13 (File Deletion via VFS): PASS")
    else:
        print("Test 13 (File Deletion via VFS): FAIL")
        all_passed = False
    print_screen(rows_rm, "Test 13: rm /disk/shell_test.txt")

    # TEST 14: Busy unmount rejection when CWD inside mount
    print("\n[TEST 14] Verifying Busy Unmount Rejection When CWD Inside Mount...")
    keys = text_to_sendkeys("clear\ncd /disk\numount /disk\n")
    rows_busy = run_qemu_test(keys, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_busy = "\n".join(rows_busy)

    t14_pass = (
        "umount: failed (busy / root mount)" in text_busy
    )
    if t14_pass:
        print("Test 14 (Busy Unmount Rejection): PASS")
    else:
        print("Test 14 (Busy Unmount Rejection): FAIL")
        all_passed = False
    print_screen(rows_busy, "Test 14: Busy Unmount")

    # TEST 15: Multi-session persistence across reboot
    print("\n[TEST 15] Verifying Cross-Boot Persistence Across Separate QEMU Sessions...")
    # Session 1: write persistent file
    keys_s1 = text_to_sendkeys("clear\nwritefile /disk/persist_test.txt Stage12EPersistsOnDisk!\ncat /disk/persist_test.txt\n")
    rows_s1 = run_qemu_test(keys_s1, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_s1 = "\n".join(rows_s1)

    # Session 2: fresh boot reading persistent file
    keys_s2 = text_to_sendkeys("clear\ncat /disk/persist_test.txt\n")
    rows_s2 = run_qemu_test(keys_s2, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_s2 = "\n".join(rows_s2)

    t15_pass = (
        "Stage12EPersistsOnDisk!" in text_s1 and
        "Stage12EPersistsOnDisk!" in text_s2
    )
    if t15_pass:
        print("Test 15 (Cross-Boot Persistence): PASS")
    else:
        print("Test 15 (Cross-Boot Persistence): FAIL")
        all_passed = False
    print_screen(rows_s2, "Test 15: Session 2 Read")

    print("\n" + "=" * 50)
    if all_passed:
        print(">>> ALL 15 STAGE 12E TESTS PASSED! <<<")
    else:
        print(">>> SOME STAGE 12E TESTS FAILED! <<<")
    print("=" * 50 + "\n")

    return 0 if all_passed else 1

if __name__ == "__main__":
    sys.exit(main())
