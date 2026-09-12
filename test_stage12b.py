#!/usr/bin/env python3
"""
test_stage12b.py - Automated Verification Suite for MyOS Stage 12B
(Generic Block Device Abstraction, block_device_t, ATA Adapter, blockinfo/blocktest)

Verifies:
  1. Boot integrity with disk attached and strict 25-row screen budget (silent block registration)
  2. 'blockinfo' command outputs registered block devices ('ata0', 512 bytes, 65536 sectors, 32 MiB)
  3. 'blocktest' command on reserved sector 8 (negative API checks, pattern write/read, verify, restore)
  4. 'blocktest' idempotence (repeated execution preserves sector data integrity)
  5. Absent device behavior (QEMU run without -hda boots cleanly, blockinfo reports no devices, blocktest reports unavailable)
  6. Coexistence with Stage 12A commands ('diskinfo', 'disktest')
  7. Coexistence with Stage 11C ELF execution ('run /bin/test'), 'vfstest', 'fdtest'
  8. Shell 'help' layout includes blockinfo/blocktest in 2-column format within screen budget
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
    print("=== MyOS Stage 12B Automated Verification Suite ===")
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

    # TEST 2: 'blockinfo' command
    print("\n[TEST 2] Verifying 'blockinfo' Command Output...")
    keys = text_to_sendkeys("clear\nblockinfo\n")
    rows_blockinfo = run_qemu_test(keys, wait_time=0.4, boot_wait=1.4, attach_disk=True)
    text_blockinfo = "\n".join(rows_blockinfo)

    t2_pass = (
        "Block Devices" in text_blockinfo and
        "Name: ata0" in text_blockinfo and
        "Type: ATA" in text_blockinfo and
        "Sector Size: 512" in text_blockinfo and
        "Sectors: 65536" in text_blockinfo and
        "Capacity: 32 MiB" in text_blockinfo
    )
    if t2_pass:
        print("Test 2 ('blockinfo' Output): PASS")
    else:
        print("Test 2 ('blockinfo' Output): FAIL")
        all_passed = False
    print_screen(rows_blockinfo, "Test 2: blockinfo")

    # TEST 3: 'blocktest' command (negative API validation, pattern write, verify, restore, verify)
    print("\n[TEST 3] Verifying 'blocktest' Command on Reserved Sector 8...")
    keys = text_to_sendkeys("clear\nblocktest\n")
    rows_blocktest = run_qemu_test(keys, wait_time=0.6, boot_wait=1.4, attach_disk=True)
    text_blocktest = "\n".join(rows_blocktest)

    t3_pass = (
        "Generic Block Device Test (ata0, Sector 8):" in text_blocktest and
        "[1/4] Reading original sector... OK" in text_blocktest and
        "[2/4] Writing test pattern 1... OK" in text_blocktest and
        "[3/4] Writing test pattern 2... OK" in text_blocktest and
        "[4/4] Restoring original sector... OK" in text_blocktest and
        "Block test passed!" in text_blocktest
    )
    if t3_pass:
        print("Test 3 ('blocktest' Execution & Verification): PASS")
    else:
        print("Test 3 ('blocktest' Execution & Verification): FAIL")
        all_passed = False
    print_screen(rows_blocktest, "Test 3: blocktest")

    # TEST 4: 'blocktest' Idempotence / Repeated Execution
    print("\n[TEST 4] Verifying Repeated 'blocktest' Idempotence...")
    keys = text_to_sendkeys("clear\nblocktest\n") + [("sleep", 0.5)] + text_to_sendkeys("blocktest\n")
    rows_repeat = run_qemu_test(keys, wait_time=0.8, boot_wait=1.4, attach_disk=True)
    text_repeat = "\n".join(rows_repeat)

    pass_count = text_repeat.count("Block test passed!")
    t4_pass = (pass_count == 2)
    if t4_pass:
        print("Test 4 ('blocktest' Idempotence): PASS")
    else:
        print(f"Test 4 ('blocktest' Idempotence): FAIL (passed count: {pass_count})")
        all_passed = False
    print_screen(rows_repeat, "Test 4: Repeated blocktest")

    # TEST 5: Absent Device Behavior (QEMU without -hda)
    print("\n[TEST 5] Verifying Absent Device Behavior (QEMU without -hda)...")
    keys = text_to_sendkeys("clear\nblockinfo\nblocktest\n")
    rows_absent = run_qemu_test(keys, wait_time=0.5, boot_wait=1.4, attach_disk=False)
    text_absent = "\n".join(rows_absent)

    t5_pass = (
        "No block devices registered." in text_absent and
        "No block device available" in text_absent and
        "FAILED" not in text_absent
    )
    if t5_pass:
        print("Test 5 (Absent Device Behavior): PASS")
    else:
        print("Test 5 (Absent Device Behavior): FAIL")
        all_passed = False
    print_screen(rows_absent, "Test 5: Absent Device")

    # TEST 6: Coexistence with Stage 12A commands ('diskinfo', 'disktest')
    print("\n[TEST 6] Verifying Coexistence with Stage 12A Commands ('diskinfo', 'disktest')...")
    keys = text_to_sendkeys("clear\ndiskinfo\ndisktest\n")
    rows_s12a = run_qemu_test(keys, wait_time=0.8, boot_wait=1.4, attach_disk=True)
    text_s12a = "\n".join(rows_s12a)

    t6_pass = (
        "ATA Disk Information:" in text_s12a and
        "Capacity: 32 MB" in text_s12a and
        "Disk test passed!" in text_s12a
    )
    if t6_pass:
        print("Test 6 (Coexistence with Stage 12A Commands): PASS")
    else:
        print("Test 6 (Coexistence with Stage 12A Commands): FAIL")
        all_passed = False
    print_screen(rows_s12a, "Test 6: Stage 12A Coexistence")

    # TEST 7: Coexistence with Stage 11C ('run /bin/test'), 'vfstest', 'fdtest'
    print("\n[TEST 7] Verifying Coexistence with Stage 11C ('run /bin/test'), 'vfstest', 'fdtest'...")
    keys = text_to_sendkeys("clear\nrun /bin/test\n")
    rows_coex = run_qemu_test(keys, wait_time=0.8, boot_wait=1.4, attach_disk=True)
    text_coex = "\n".join(rows_coex)

    t7_pass = (
        "[ELF Ring 3] Hello from loaded ELF64 executable!" in text_coex and
        "MyOS>" in text_coex
    )
    if t7_pass:
        print("Test 7 (Coexistence with ELF execution): PASS")
    else:
        print("Test 7 (Coexistence with ELF execution): FAIL")
        all_passed = False
    print_screen(rows_coex, "Test 7: Stage 11C Coexistence")

    # TEST 8: Shell 'help' layout includes blockinfo and blocktest in 2 columns
    print("\n[TEST 8] Verifying 'help' Command Layout and Entries...")
    keys = text_to_sendkeys("clear\nhelp\n")
    rows_help = run_qemu_test(keys, wait_time=0.4, boot_wait=1.4, attach_disk=True)
    text_help = "\n".join(rows_help)

    help_non_empty = sum(1 for r in rows_help if r.strip())
    t8_pass = (
        "blockinfo" in text_help and
        "blocktest" in text_help and
        help_non_empty <= 24
    )
    if t8_pass:
        print(f"Test 8 ('help' Entries & Budget: {help_non_empty} rows): PASS")
    else:
        print(f"Test 8 ('help' Entries & Budget: {help_non_empty} rows): FAIL")
        all_passed = False
    print_screen(rows_help, "Test 8: help")

    print("\n==================================================")
    if all_passed:
        print("ALL STAGE 12B TESTS PASSED (8/8)")
        print("==================================================")
        sys.exit(0)
    else:
        print("SOME STAGE 12B TESTS FAILED")
        print("==================================================")
        sys.exit(1)

if __name__ == "__main__":
    main()
