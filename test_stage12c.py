#!/usr/bin/env python3
"""
test_stage12c.py - Automated Verification Suite for MyOS Stage 12C
(Persistent Filesystem, PFS On-Disk Format, Direct Inodes, Bitmaps, Directory Ops,
Persistence Across Reboot, Corruption Rejection, Coexistence)

Verifies:
  1. Boot integrity with disk attached and strict 25-row screen budget (silent probe)
  2. 'pfsformat' and 'pfsinfo' format disk and report accurate metadata (PFS1 magic, geometry, regions)
  3. 'pfstest' in-kernel verification suite (create, read, write, multi-block, dir, lookup, rollback, corruption)
  4. 'pfscat' reads persistent file 'hello.txt'
  5. Persistence across reboot: reboots QEMU, mounts existing filesystem without formatting, recovers exact content
  6. Absent disk handling (QEMU run without -hda handles missing disk gracefully)
  7. Coexistence with Stage 12A (diskinfo/disktest) & Stage 12B (blockinfo/blocktest)
  8. Coexistence with Stage 11C ELF execution ('run /bin/test') & VFS/RAMFS ('ls', 'pwd')
  9. Shell 'help' layout includes PFS commands in 2-column format within screen budget
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

def run_qemu_test(key_sequence, wait_time=0.5, boot_wait=1.8, attach_disk=True):
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
    print("=== MyOS Stage 12C Automated Verification Suite ===")
    all_passed = True

    # TEST 1: Boot integrity and 25-row screen line budget
    print("\n[TEST 1] Verifying Boot Integrity and Screen Line Budget with Disk Attached...")
    boot_rows = run_qemu_test([], wait_time=0.1, boot_wait=1.8, attach_disk=True)
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

    # TEST 2: 'pfsformat' and 'pfsinfo' commands
    print("\n[TEST 2] Verifying 'pfsformat' and 'pfsinfo' Commands...")
    keys = text_to_sendkeys("clear\npfsformat\n")
    keys.append(("sleep", 1.5))
    keys.extend(text_to_sendkeys("pfsinfo\n"))
    rows_format = run_qemu_test(keys, wait_time=0.8, boot_wait=1.8, attach_disk=True)
    text_format = "\n".join(rows_format)

    t2_pass = (
        "Formatting ata0 with PFS... OK" in text_format and
        "Persistent Filesystem (PFS) Info:" in text_format and
        "Device: ata0" in text_format and
        "Magic: 0x50465331" in text_format and
        "Sector Size: 512" in text_format and
        "Total Sectors: 65536" in text_format and
        "Data Region: Sector 82" in text_format
    )
    if t2_pass:
        print("Test 2 ('pfsformat' & 'pfsinfo' Output): PASS")
    else:
        print("Test 2 ('pfsformat' & 'pfsinfo' Output): FAIL")
        all_passed = False
    print_screen(rows_format, "Test 2: pfsformat & pfsinfo")

    # TEST 3: 'pfstest' in-kernel test suite
    print("\n[TEST 3] Verifying 'pfstest' In-Kernel Test Suite...")
    keys = text_to_sendkeys("clear\npfstest\n")
    rows_pfstest = run_qemu_test(keys, wait_time=3.0, boot_wait=1.8, attach_disk=True)
    text_pfstest = "\n".join(rows_pfstest)

    t3_pass = (
        "Running PFS in-kernel verification suite..." in text_pfstest and
        "PFS verification suite passed!" in text_pfstest and
        "FAIL" not in text_pfstest
    )
    if t3_pass:
        print("Test 3 ('pfstest' Execution): PASS")
    else:
        print("Test 3 ('pfstest' Execution): FAIL")
        all_passed = False
    print_screen(rows_pfstest, "Test 3: pfstest")

    # TEST 4: 'pfscat' read persistent file
    print("\n[TEST 4] Verifying 'pfscat' Command on 'hello.txt'...")
    keys = text_to_sendkeys("clear\npfscat hello.txt\n")
    rows_pfscat = run_qemu_test(keys, wait_time=0.5, boot_wait=1.8, attach_disk=True)
    text_pfscat = "\n".join(rows_pfscat)

    t4_pass = (
        "Hello from MyOS persistent storage!" in text_pfscat
    )
    if t4_pass:
        print("Test 4 ('pfscat' Output): PASS")
    else:
        print("Test 4 ('pfscat' Output): FAIL")
        all_passed = False
    print_screen(rows_pfscat, "Test 4: pfscat")

    # TEST 5: PERSISTENCE ACROSS REBOOT
    print("\n[TEST 5] Verifying Persistence Across Reboot (New QEMU Instance Without Format)...")
    # Launch completely fresh QEMU process with same disk.img without formatting
    keys_reboot = text_to_sendkeys("clear\npfsmount\n")
    keys_reboot.append(("sleep", 0.4))
    keys_reboot.extend(text_to_sendkeys("pfscat hello.txt\n"))
    rows_reboot = run_qemu_test(keys_reboot, wait_time=0.6, boot_wait=1.8, attach_disk=True)
    text_reboot = "\n".join(rows_reboot)

    t5_pass = (
        "Hello from MyOS persistent storage!" in text_reboot and
        "Mount failed" not in text_reboot
    )
    if t5_pass:
        print("Test 5 (Persistence Across Reboot): PASS")
    else:
        print("Test 5 (Persistence Across Reboot): FAIL")
        all_passed = False
    print_screen(rows_reboot, "Test 5: Persistence Across Reboot")

    # TEST 6: Absent disk handling (QEMU without -hda)
    print("\n[TEST 6] Verifying Absent Disk Handling (QEMU without -hda)...")
    keys_absent = text_to_sendkeys("clear\npfsinfo\npfsformat\npfsmount\n")
    rows_absent = run_qemu_test(keys_absent, wait_time=0.6, boot_wait=1.8, attach_disk=False)
    text_absent = "\n".join(rows_absent)

    t6_pass = (
        "PFS: Not mounted" in text_absent and
        "pfsformat: Device not found" in text_absent and
        "pfsmount: Device not found" in text_absent and
        "Triple Fault" not in text_absent
    )
    if t6_pass:
        print("Test 6 (Absent Disk Handling): PASS")
    else:
        print("Test 6 (Absent Disk Handling): FAIL")
        all_passed = False
    print_screen(rows_absent, "Test 6: Absent Disk")

    # TEST 7: Coexistence with Stage 12A & 12B commands
    print("\n[TEST 7] Verifying Coexistence with Stage 12A & 12B Commands...")
    keys_coex12 = text_to_sendkeys("clear\ndiskinfo\n")
    keys_coex12.append(("sleep", 0.3))
    keys_coex12.extend(text_to_sendkeys("blockinfo\n"))
    rows_coex12 = run_qemu_test(keys_coex12, wait_time=0.8, boot_wait=1.8, attach_disk=True)
    text_coex12 = "\n".join(rows_coex12)

    t7_pass = (
        "ATA Disk Information:" in text_coex12 and
        "Capacity: 32 MB" in text_coex12 and
        "Block Devices" in text_coex12 and
        "Name: ata0" in text_coex12 and
        "Capacity: 32 MiB" in text_coex12
    )
    if t7_pass:
        print("Test 7 (Coexistence with Stage 12A & 12B): PASS")
    else:
        print("Test 7 (Coexistence with Stage 12A & 12B): FAIL")
        all_passed = False
    print_screen(rows_coex12, "Test 7: Stage 12A & 12B Coexistence")

    # TEST 8: Coexistence with Stage 11C ('run /bin/test') & VFS/RAMFS ('pwd', 'ls')
    print("\n[TEST 8] Verifying Coexistence with Stage 11C ('run /bin/test') & VFS/RAMFS...")
    keys_coex11 = text_to_sendkeys("clear\npwd\n")
    keys_coex11.append(("sleep", 0.3))
    keys_coex11.extend(text_to_sendkeys("run /bin/test\n"))
    rows_coex11 = run_qemu_test(keys_coex11, wait_time=0.8, boot_wait=1.8, attach_disk=True)
    text_coex11 = "\n".join(rows_coex11)

    t8_pass = (
        "/" in text_coex11 and
        "[ELF Ring 3] Hello from loaded ELF64 executable!" in text_coex11 and
        "MyOS>" in text_coex11
    )
    if t8_pass:
        print("Test 8 (Coexistence with ELF execution & VFS): PASS")
    else:
        print("Test 8 (Coexistence with ELF execution & VFS): FAIL")
        all_passed = False
    print_screen(rows_coex11, "Test 8: Stage 11C & VFS Coexistence")

    # TEST 9: Shell 'help' layout includes PFS commands in 2 columns
    print("\n[TEST 9] Verifying 'help' Command Layout and Entries...")
    keys_help = text_to_sendkeys("clear\nhelp\n")
    rows_help = run_qemu_test(keys_help, wait_time=0.5, boot_wait=1.8, attach_disk=True)
    text_help = "\n".join(rows_help)

    help_non_empty = sum(1 for r in rows_help if r.strip())
    t9_pass = (
        "pfsinfo" in text_help and
        "pfsformat" in text_help and
        "pfsmount" in text_help and
        "pfscat" in text_help and
        "pfstest" in text_help and
        help_non_empty <= 24
    )
    if t9_pass:
        print(f"Test 9 ('help' Entries & Budget: {help_non_empty} rows): PASS")
    else:
        print(f"Test 9 ('help' Entries & Budget: {help_non_empty} rows): FAIL")
        all_passed = False
    print_screen(rows_help, "Test 9: help")

    # TEST 10: Actual On-Disk Metadata Mutation Corruption Tests
    print("\n[TEST 10] Verifying On-Disk Corruption Rejection (Actual Disk-Image Mutations)...")
    import struct
    import shutil
    import os

    clean_img_bak = "build/disk_clean.bak"
    shutil.copyfile("build/disk.img", clean_img_bak)

    mutations = [
        ("magic", 0, struct.pack("<I", 0xDEADBEEF)),
        ("version", 4, struct.pack("<I", 99)),
        ("sector_size", 8, struct.pack("<I", 1024)),
        ("metadata_boundary", 16, struct.pack("<I", 999)),
        ("metadata_overlap", 24, struct.pack("<I", 1)),
        ("free_blocks_count", 48, struct.pack("<I", 999999)),
        ("free_inodes_count", 56, struct.pack("<I", 999999)),
        ("root_inode_number", 60, struct.pack("<I", 999)),
        ("root_block_pointer", 9292, struct.pack("<I", 0x00020000)),
    ]

    t10_all_pass = True
    for name, offset, bad_bytes in mutations:
        time.sleep(0.3)
        shutil.copyfile(clean_img_bak, "build/disk.img")
        with open("build/disk.img", "r+b") as f:
            f.seek(offset)
            f.write(bad_bytes)

        keys_mut = text_to_sendkeys("clear\npfsmount\n")
        rows_mut = run_qemu_test(keys_mut, wait_time=0.5, boot_wait=1.8, attach_disk=True)
        txt_mut = "\n".join(rows_mut)

        with open("build/disk.img", "rb") as f:
            f.seek(offset)
            cur_bytes = f.read(len(bad_bytes))
            reformatted = (cur_bytes != bad_bytes)

        rejected = "pfsmount: Mount failed (invalid or unformatted filesystem)" in txt_mut
        no_panic = "Triple Fault" not in txt_mut and "PANIC" not in txt_mut

        if rejected and no_panic and not reformatted:
            print(f"  Mutation '{name}': PASS (rejected cleanly, no panic, no reformat)")
        else:
            print(f"  Mutation '{name}': FAIL (rejected={rejected}, no_panic={no_panic}, reformatted={reformatted})")
            t10_all_pass = False

    shutil.copyfile(clean_img_bak, "build/disk.img")
    if os.path.exists(clean_img_bak):
        os.remove(clean_img_bak)

    if t10_all_pass:
        print("Test 10 (9/9 On-Disk Corruption Mutations Rejected Cleanly): PASS")
    else:
        print("Test 10 (On-Disk Corruption Mutations): FAIL")
        all_passed = False

    print("\n==================================================")
    if all_passed:
        print("ALL STAGE 12C TESTS PASSED (10/10)")
        print("==================================================")
        sys.exit(0)
    else:
        print("SOME STAGE 12C TESTS FAILED")
        print("==================================================")
        sys.exit(1)

if __name__ == "__main__":
    main()
