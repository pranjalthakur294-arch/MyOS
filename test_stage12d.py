#!/usr/bin/env python3
"""
test_stage12d.py - Automated Verification Suite for MyOS Stage 12D
(Filesystem Mounting Subsystem: Filesystem Types, Instances, Mount Records,
Bounded Mount Table, Root RAMFS Protection, PFS Mount Adapter, Validation,
Rollback, Unmount Lifecycle, Slot Reuse, No Heap Leaks, Coexistence)

Verifies:
  1. Boot integrity with disk attached and strict 25-row screen budget (silent probe)
  2. Shell command table budget check ('help' fits in 2-column layout within 24 rows)
  3. 'mounttest' in-kernel verification suite (all 22 core checks passed)
  4. 'mount' with no arguments lists active mounts ('/' on none type ramfs, '/disk' on ata0 type pfs)
  5. Duplicate mount rejection ('mount pfs ata0 /disk' reports already mounted)
  6. Invalid filesystem rejection ('mount badfs ata0 /disk' reports not found)
  7. Invalid device rejection ('mount pfs baddev /disk' reports device not found)
  8. Unresolvable mount-point rejection ('mount pfs ata0 /not/found' reports not found)
  9. Successful unmount lifecycle ('umount /disk' unmounts; removed from 'mount' listing)
  10. Root mount protection ('umount /' rejected with busy / root mount error)
  11. Double unmount rejection ('umount /disk' on unmounted target rejected)
  12. Mount slot reusability ('mount pfs ata0 /disk' successfully re-mounts)
  13. Persistence across reboot (PFS volume discovered and mounted upon reboot)
  14. Absent disk handling (boots cleanly to shell without panic when disk is omitted)
  15. Coexistence with Stage 11C ELF execution ('run /bin/test') & Stage 11D CWD/path ('ls /', 'pwd')
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
    print("=== MyOS Stage 12D Automated Verification Suite ===")
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
        "mount" in help_text and
        help_non_empty <= 24 and
        ("MyOS>" in help_rows[23] or "MyOS>" in help_rows[24])
    )
    if t2_pass:
        print("Test 2 (Shell Command Table Layout): PASS")
    else:
        print("Test 2 (Shell Command Table Layout): FAIL")
        all_passed = False
    print_screen(help_rows, "Test 2: help Screen")

    # TEST 3: 'mounttest' in-kernel verification suite
    print("\n[TEST 3] Verifying 'mounttest' In-Kernel Verification Suite (22 Checks)...")
    keys = text_to_sendkeys("clear\nmounttest\n")
    rows_test = run_qemu_test(keys, wait_time=3.5, boot_wait=2.0, attach_disk=True)
    text_test = "\n".join(rows_test)

    t3_pass = (
        "Running mount in-kernel verification suite..." in text_test and
        "Mount verification suite passed!" in text_test and
        "FAIL" not in text_test
    )
    if t3_pass:
        print("Test 3 ('mounttest' Execution): PASS")
    else:
        print("Test 3 ('mounttest' Execution): FAIL")
        all_passed = False
    print_screen(rows_test, "Test 3: mounttest")

    # TEST 4: 'mount' command with no arguments
    print("\n[TEST 4] Verifying 'mount' Command Output...")
    keys = text_to_sendkeys("clear\nmount\n")
    rows_mount = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    text_mount = "\n".join(rows_mount)

    t4_pass = (
        "Active mounts:" in text_mount and
        "/ on none type ramfs" in text_mount and
        "/disk on ata0 type pfs" in text_mount
    )
    if t4_pass:
        print("Test 4 ('mount' Listing): PASS")
    else:
        print("Test 4 ('mount' Listing): FAIL")
        all_passed = False
    print_screen(rows_mount, "Test 4: mount Listing")

    # TEST 5: Duplicate mount rejection via shell
    print("\n[TEST 5] Verifying Duplicate Mount Rejection via Shell...")
    keys = text_to_sendkeys("clear\nmount pfs ata0 /disk\n")
    rows_dup = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    text_dup = "\n".join(rows_dup)

    t5_pass = (
        "mount: failed (already mounted)" in text_dup
    )
    if t5_pass:
        print("Test 5 (Duplicate Mount Rejection): PASS")
    else:
        print("Test 5 (Duplicate Mount Rejection): FAIL")
        all_passed = False
    print_screen(rows_dup, "Test 5: Duplicate Mount")

    # TEST 6: Invalid filesystem rejection via shell
    print("\n[TEST 6] Verifying Invalid Filesystem Rejection via Shell...")
    keys = text_to_sendkeys("clear\nmount badfs ata0 /disk\n")
    rows_badfs = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    text_badfs = "\n".join(rows_badfs)

    t6_pass = (
        "mount: failed (not found)" in text_badfs
    )
    if t6_pass:
        print("Test 6 (Invalid Filesystem Rejection): PASS")
    else:
        print("Test 6 (Invalid Filesystem Rejection): FAIL")
        all_passed = False
    print_screen(rows_badfs, "Test 6: Invalid Filesystem")

    # TEST 7: Invalid device rejection via shell
    print("\n[TEST 7] Verifying Invalid Device Rejection via Shell...")
    keys = text_to_sendkeys("clear\nmount pfs nonexistent_dev /disk\n")
    rows_baddev = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    text_baddev = "\n".join(rows_baddev)

    t7_pass = (
        "mount: failed (device not found)" in text_baddev
    )
    if t7_pass:
        print("Test 7 (Invalid Device Rejection): PASS")
    else:
        print("Test 7 (Invalid Device Rejection): FAIL")
        all_passed = False
    print_screen(rows_baddev, "Test 7: Invalid Device")

    # TEST 8: Unresolvable mount-point rejection via shell
    print("\n[TEST 8] Verifying Unresolvable Mount-Point Rejection via Shell...")
    keys = text_to_sendkeys("clear\nmount pfs ata0 /not/a/real/path\n")
    rows_badpath = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    text_badpath = "\n".join(rows_badpath)

    t8_pass = (
        "mount: failed (not found)" in text_badpath
    )
    if t8_pass:
        print("Test 8 (Unresolvable Mount-Point Rejection): PASS")
    else:
        print("Test 8 (Unresolvable Mount-Point Rejection): FAIL")
        all_passed = False
    print_screen(rows_badpath, "Test 8: Unresolvable Mount-Point")

    # TEST 9: Successful unmount lifecycle
    print("\n[TEST 9] Verifying 'umount /disk' Lifecycle...")
    keys = text_to_sendkeys("clear\numount /disk\nmount\n")
    rows_umount = run_qemu_test(keys, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_umount = "\n".join(rows_umount)

    t9_pass = (
        "Unmounted /disk" in text_umount and
        "/ on none type ramfs" in text_umount and
        "/disk" not in text_umount.split("Active mounts:")[1]
    )
    if t9_pass:
        print("Test 9 ('umount /disk' Lifecycle): PASS")
    else:
        print("Test 9 ('umount /disk' Lifecycle): FAIL")
        all_passed = False
    print_screen(rows_umount, "Test 9: umount Lifecycle")

    # TEST 10: Root mount protection
    print("\n[TEST 10] Verifying Root Mount Protection Against Unmount...")
    keys = text_to_sendkeys("clear\numount /\n")
    rows_root_prot = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    text_root_prot = "\n".join(rows_root_prot)

    t10_pass = (
        "umount: failed (busy / root mount)" in text_root_prot
    )
    if t10_pass:
        print("Test 10 (Root Mount Protection): PASS")
    else:
        print("Test 10 (Root Mount Protection): FAIL")
        all_passed = False
    print_screen(rows_root_prot, "Test 10: Root Mount Protection")

    # TEST 11: Double unmount rejection
    print("\n[TEST 11] Verifying Double Unmount Rejection...")
    keys = text_to_sendkeys("clear\numount /disk\numount /disk\n")
    rows_double_u = run_qemu_test(keys, wait_time=0.3, boot_wait=2.0, attach_disk=True)
    text_double_u = "\n".join(rows_double_u)

    t11_pass = (
        "umount: failed (not mounted)" in text_double_u
    )
    if t11_pass:
        print("Test 11 (Double Unmount Rejection): PASS")
    else:
        print("Test 11 (Double Unmount Rejection): FAIL")
        all_passed = False
    print_screen(rows_double_u, "Test 11: Double Unmount")

    # TEST 12: Mount slot reusability
    print("\n[TEST 12] Verifying Mount Slot Reusability...")
    keys = text_to_sendkeys("clear\numount /disk\nmount pfs ata0 /disk\nmount\n")
    rows_reuse = run_qemu_test(keys, wait_time=0.4, boot_wait=2.0, attach_disk=True)
    text_reuse = "\n".join(rows_reuse)

    t12_pass = (
        "Mounted /disk" in text_reuse and
        "/disk on ata0 type pfs" in text_reuse
    )
    if t12_pass:
        print("Test 12 (Mount Slot Reusability): PASS")
    else:
        print("Test 12 (Mount Slot Reusability): FAIL")
        all_passed = False
    print_screen(rows_reuse, "Test 12: Mount Slot Reuse")

    # TEST 13: Persistence across reboot
    print("\n[TEST 13] Verifying Persistence Across Reboot (Format -> pfstest -> Reboot -> Mount Detection)...")
    # First boot: run pfstest (which formats disk, runs verification, and creates hello.txt)
    keys_fmt = text_to_sendkeys("clear\npfstest\n")
    run_qemu_test(keys_fmt, wait_time=3.5, boot_wait=2.0, attach_disk=True)

    # Second boot: clean reboot, disk should automatically be detected and mounted at /disk
    keys_reboot = text_to_sendkeys("clear\nmount\npfscat hello.txt\n")
    rows_persist = run_qemu_test(keys_reboot, wait_time=0.5, boot_wait=2.0, attach_disk=True)
    text_persist = "\n".join(rows_persist)

    t13_pass = (
        "/disk on ata0 type pfs" in text_persist and
        "Hello from MyOS persistent storage!" in text_persist
    )
    if t13_pass:
        print("Test 13 (Persistence Across Reboot): PASS")
    else:
        print("Test 13 (Persistence Across Reboot): FAIL")
        all_passed = False
    print_screen(rows_persist, "Test 13: Reboot Persistence")

    # TEST 14: Absent disk graceful handling
    print("\n[TEST 14] Verifying Absent Disk Graceful Handling (QEMU without -hda)...")
    rows_nodisk = run_qemu_test(text_to_sendkeys("clear\nmount\n"), wait_time=0.3, boot_wait=2.0, attach_disk=False)
    text_nodisk = "\n".join(rows_nodisk)

    t14_pass = (
        "Active mounts:" in text_nodisk and
        "/ on none type ramfs" in text_nodisk and
        "/disk" not in text_nodisk and
        "MyOS>" in text_nodisk
    )
    if t14_pass:
        print("Test 14 (Absent Disk Graceful Handling): PASS")
    else:
        print("Test 14 (Absent Disk Graceful Handling): FAIL")
        all_passed = False
    print_screen(rows_nodisk, "Test 14: Absent Disk")

    # TEST 15: Coexistence with previous stages
    print("\n[TEST 15] Verifying Coexistence with Stage 11C ELF, Stage 11D CWD/path, Stage 12B Block, Stage 12A Disk...")
    keys = text_to_sendkeys("clear\nrun /bin/test\n")
    keys.append(("sleep", 0.5))
    keys.extend(text_to_sendkeys("pwd\nls /\nblockinfo\n"))
    rows_coexist = run_qemu_test(keys, wait_time=1.0, boot_wait=2.0, attach_disk=True)
    text_coexist = "\n".join(rows_coexist)

    t15_pass = (
        "Hello from loaded ELF64 executable!" in text_coexist and
        "pwd\n/" in text_coexist and
        "bin" in text_coexist and
        "disk" in text_coexist and
        "Block Devices" in text_coexist and
        "Name: ata0" in text_coexist
    )
    if t15_pass:
        print("Test 15 (System Coexistence): PASS")
    else:
        print("Test 15 (System Coexistence): FAIL")
        all_passed = False
    print_screen(rows_coexist, "Test 15: Coexistence")

    print("\n==================================================")
    if all_passed:
        print(">>> ALL 15 STAGE 12D TESTS PASSED! <<<")
        print("==================================================")
        sys.exit(0)
    else:
        print(">>> SOME STAGE 12D TESTS FAILED! <<<")
        print("==================================================")
        sys.exit(1)

if __name__ == "__main__":
    main()
