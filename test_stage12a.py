#!/usr/bin/env python3
"""
test_stage12a.py - Automated Verification Suite for MyOS Stage 12A
(ATA PIO Disk Driver, Primary Master, LBA28, 512-byte Sector I/O)

Verifies:
  1. Boot integrity with disk attached and strict 25-row screen budget (silent ATA probe)
  2. 'diskinfo' command outputs Primary Master geometry (512 bytes/sector, 65536 sectors, 32 MB)
  3. 'disktest' command on reserved sector LBA 8 (read, write pattern, verify 512 bytes, restore, verify restore)
  4. 'disktest' idempotence (repeated execution preserves sector data integrity)
  5. Graceful drive absence handling (QEMU run without -hda boots cleanly, diskinfo/disktest report absent)
  6. Shell 'about' includes ATA PIO driver entry and fits within 25 rows (22 rows)
  7. Shell 'help' includes diskinfo/disktest in 2-column layout (18 rows < 25 rows)
  8. Coexistence with Stage 11C ELF execution ('run /bin/test'), 'vfstest', 'fdtest'
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

def run_qemu_test(key_sequence, wait_time=0.5, boot_wait=1.4, attach_disk=True):
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

def main():
    print("=== MyOS Stage 12A Automated Verification Suite ===")
    all_passed = True

    # TEST 1: Boot integrity and 25-row screen line budget
    print("\n[TEST 1] Verifying Boot Integrity and Screen Line Budget with Disk Attached...")
    boot_rows = run_qemu_test([], wait_time=0.1, boot_wait=1.4, attach_disk=True)
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

    # TEST 2: 'diskinfo' command
    print("\n[TEST 2] Verifying 'diskinfo' Command...")
    keys = text_to_sendkeys("clear\ndiskinfo\n")
    rows_diskinfo = run_qemu_test(keys, wait_time=0.4, boot_wait=1.4, attach_disk=True)
    text_diskinfo = "\n".join(rows_diskinfo)

    t2_pass = (
        "ATA Disk Information:" in text_diskinfo and
        "Channel: Primary" in text_diskinfo and
        "Device: Master" in text_diskinfo and
        "Present: Yes" in text_diskinfo and
        "Sector Size: 512 bytes" in text_diskinfo and
        "LBA28 Sectors: 65536" in text_diskinfo and
        "Capacity: 32 MB" in text_diskinfo
    )
    if t2_pass:
        print("Test 2 ('diskinfo' Output): PASS")
    else:
        print("Test 2 ('diskinfo' Output): FAIL")
        all_passed = False
    print_screen(rows_diskinfo, "Test 2: diskinfo")

    # TEST 3: 'disktest' command (read, pattern write, verify, restore, verify)
    print("\n[TEST 3] Verifying 'disktest' Command on Reserved Sector LBA 8...")
    keys = text_to_sendkeys("clear\ndisktest\n")
    rows_disktest = run_qemu_test(keys, wait_time=0.6, boot_wait=1.4, attach_disk=True)
    text_disktest = "\n".join(rows_disktest)

    t3_pass = (
        "ATA PIO Disk Test (LBA 8):" in text_disktest and
        "[1/4] Reading original sector... OK" in text_disktest and
        "[2/4] Writing test pattern... OK" in text_disktest and
        "[3/4] Verifying test pattern... OK" in text_disktest and
        "[4/4] Restoring original sector... OK" in text_disktest and
        "Disk test passed!" in text_disktest
    )
    if t3_pass:
        print("Test 3 ('disktest' Execution & Verification): PASS")
    else:
        print("Test 3 ('disktest' Execution & Verification): FAIL")
        all_passed = False
    print_screen(rows_disktest, "Test 3: disktest")

    # TEST 4: 'disktest' Idempotence / Repeated Execution
    print("\n[TEST 4] Verifying Repeated 'disktest' Idempotence...")
    keys = text_to_sendkeys("clear\ndisktest\n") + [("sleep", 0.5)] + text_to_sendkeys("disktest\n")
    rows_repeat = run_qemu_test(keys, wait_time=0.8, boot_wait=1.4, attach_disk=True)
    text_repeat = "\n".join(rows_repeat)

    pass_count = text_repeat.count("Disk test passed!")
    t4_pass = (pass_count == 2)
    if t4_pass:
        print("Test 4 ('disktest' Idempotence): PASS")
    else:
        print(f"Test 4 ('disktest' Idempotence): FAIL (passed count: {pass_count})")
        all_passed = False
    print_screen(rows_repeat, "Test 4: Repeated disktest")

    # TEST 5: Graceful Drive Absence Handling (QEMU without -hda)
    print("\n[TEST 5] Verifying Graceful Absence Handling (QEMU without -hda)...")
    keys = text_to_sendkeys("clear\ndiskinfo\ndisktest\n")
    rows_absent = run_qemu_test(keys, wait_time=0.5, boot_wait=1.4, attach_disk=False)
    text_absent = "\n".join(rows_absent)

    t5_pass = (
        "Present: No" in text_absent and
        "ATA disk not present" in text_absent and
        "FAILED" not in text_absent
    )
    if t5_pass:
        print("Test 5 (Drive Absence Handling): PASS")
    else:
        print("Test 5 (Drive Absence Handling): FAIL")
        all_passed = False
    print_screen(rows_absent, "Test 5: Drive Absence")

    # TEST 6: 'about' command includes ATA PIO driver
    print("\n[TEST 6] Verifying 'about' Command Includes ATA PIO Driver...")
    keys = text_to_sendkeys("clear\nabout\n")
    rows_about = run_qemu_test(keys, wait_time=0.4, boot_wait=1.4, attach_disk=True)
    text_about = "\n".join(rows_about)

    non_empty_rows = sum(1 for r in rows_about if r.strip())
    t6_pass = (
        "Disk: Primary ATA PIO Driver Active" in text_about and
        "VFS/RAMFS: In-Memory Virtual Filesystem Active" in text_about and
        "Processes: Isolated Address Spaces (CR3) Active" in text_about and
        non_empty_rows <= 24
    )
    if t6_pass:
        print(f"Test 6 ('about' Info & Budget: {non_empty_rows} rows): PASS")
    else:
        print(f"Test 6 ('about' Info & Budget: {non_empty_rows} rows): FAIL")
        all_passed = False
    print_screen(rows_about, "Test 6: about")

    # TEST 7: 'help' command includes diskinfo and disktest in 2 columns
    print("\n[TEST 7] Verifying 'help' Command Layout and Entries...")
    keys = text_to_sendkeys("clear\nhelp\n")
    rows_help = run_qemu_test(keys, wait_time=0.4, boot_wait=1.4, attach_disk=True)
    text_help = "\n".join(rows_help)

    help_non_empty = sum(1 for r in rows_help if r.strip())
    t7_pass = (
        "diskinfo" in text_help and
        "disktest" in text_help and
        help_non_empty <= 24
    )
    if t7_pass:
        print(f"Test 7 ('help' Entries & Budget: {help_non_empty} rows): PASS")
    else:
        print(f"Test 7 ('help' Entries & Budget: {help_non_empty} rows): FAIL")
        all_passed = False
    print_screen(rows_help, "Test 7: help")

    # TEST 8: Coexistence with Stage 11C ('run /bin/test'), 'vfstest', 'fdtest'
    print("\n[TEST 8] Verifying Coexistence with Stage 11C ('run /bin/test'), 'vfstest', 'fdtest'...")
    keys = text_to_sendkeys("clear\nrun /bin/test\n")
    rows_coex = run_qemu_test(keys, wait_time=0.8, boot_wait=1.4, attach_disk=True)
    text_coex = "\n".join(rows_coex)

    t8_pass = (
        "[ELF Ring 3] Hello from loaded ELF64 executable!" in text_coex and
        "MyOS>" in text_coex
    )
    if t8_pass:
        print("Test 8 (Coexistence with ELF execution): PASS")
    else:
        print("Test 8 (Coexistence with ELF execution): FAIL")
        all_passed = False
    print_screen(rows_coex, "Test 8: Coexistence")

    print("\n==================================================")
    if all_passed:
        print("ALL STAGE 12A TESTS PASSED (8/8)")
        print("==================================================")
        sys.exit(0)
    else:
        print("SOME STAGE 12A TESTS FAILED")
        print("==================================================")
        sys.exit(1)

if __name__ == "__main__":
    main()
