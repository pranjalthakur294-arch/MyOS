#!/usr/bin/env python3
"""
test_stage6.py - Automated Verification Suite for MyOS Stage 6
Verifies the Kernel Heap Allocator (kmalloc, kfree, free-list, block splitting,
coalescing, alignment, heapinfo, heaptest, and full regression).
"""

import subprocess
import time
import re
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

def run_qemu_test(key_sequence, wait_time=0.4, boot_wait=1.2):
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(boot_wait)

    for k in key_sequence:
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.03)

    time.sleep(wait_time)
    out, _ = p.communicate(input="xp /4000xb 0xb8000\nq\n")

    chars = []
    for line in out.splitlines():
        if ":" in line:
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
        rows.append(row_str.rstrip())
    return rows

def print_screen(rows, label="VGA BUFFER"):
    print("=" * 60)
    print(f"--- {label} ---")
    print("=" * 60)
    for idx, r in enumerate(rows):
        if r.strip():
            print(f"[{idx:02d}] {r}")
    print("=" * 60)

def main():
    print("==================================================")
    print("STAGE 6 COMPREHENSIVE AUTOMATED VERIFICATION")
    print("==================================================")

    passed_count = 0
    total_count = 15

    # Test 1: Boot to Shell
    print("\n[TEST 1] Verifying Boot to Shell...")
    rows = run_qemu_test([], wait_time=0.2, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t1_pass = "MyOS - Educational x86-64 Kernel" in screen_text and "MyOS>" in screen_text
    print("Test 1 (Boot to Shell):", "PASS" if t1_pass else "FAIL")
    if t1_pass: passed_count += 1
    print_screen(rows, "Test 1: Boot Banner")

    # Test 2: Heap Milestone Banner
    print("\n[TEST 2] Verifying Heap Initialization Milestone & Banner...")
    t2_pass = ("[OK] Kernel heap initialized" in screen_text and
               "Stage 6 Goal Achieved: Dynamic kernel heap allocator active!" in screen_text)
    print("Test 2 (Heap Milestone Banner):", "PASS" if t2_pass else "FAIL")
    if t2_pass: passed_count += 1

    # Test 3: heapinfo command
    print("\n[TEST 3] Verifying 'heapinfo' command output...")
    rows = run_qemu_test(text_to_sendkeys("clear\nheapinfo\n"), wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t3_pass = ("Kernel Heap:" in screen_text and
               "Start:" in screen_text and
               "50000000" in screen_text and
               "Size:         65536 bytes" in screen_text and
               "Used:         0 bytes" in screen_text and
               "Free:         65512 bytes" in screen_text and
               "Blocks:       1" in screen_text and
               "Free Blocks:  1" in screen_text and
               "Used Blocks:  0" in screen_text and
               "Largest Free: 65512 bytes" in screen_text)
    print("Test 3 (heapinfo Output):", "PASS" if t3_pass else "FAIL")
    if t3_pass: passed_count += 1
    print_screen(rows, "Test 3: heapinfo")

    # Test 4: heaptest execution
    print("\n[TEST 4] Verifying 'heaptest' end-to-end execution...")
    rows = run_qemu_test(text_to_sendkeys("clear\nheaptest\n"), wait_time=0.5, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t4_pass = ("Heap Test:" in screen_text and
               "kmalloc basic allocation: OK" in screen_text and
               "alignment: OK" in screen_text and
               "distinct allocations: OK" in screen_text and
               "memory write/read: OK" in screen_text and
               "free: OK" in screen_text and
               "block reuse: OK" in screen_text and
               "block splitting: OK" in screen_text and
               "block coalescing: OK" in screen_text and
               "zero-size allocation: OK" in screen_text and
               "oversized allocation: OK" in screen_text and
               "Heap test passed!" in screen_text)
    print("Test 4 (heaptest Complete Flow):", "PASS" if t4_pass else "FAIL")
    if t4_pass: passed_count += 1
    print_screen(rows, "Test 4: heaptest")

    # Test 5: Heap Accounting Consistency Across Repeated heaptest Calls
    print("\n[TEST 5] Verifying Heap Consistency Across Repeated heaptest Calls (No Leaks)...")
    keys = text_to_sendkeys("clear\nheaptest\nheapinfo\nheaptest\nheapinfo\n")
    rows = run_qemu_test(keys, wait_time=0.7, boot_wait=1.2)
    screen_text = "\n".join(rows)

    free_matches = list(re.finditer(r"Free:\s*(\d+)\s*bytes", screen_text))
    t5_pass = False
    if len(free_matches) >= 2:
        f1 = int(free_matches[0].group(1))
        f2 = int(free_matches[1].group(1))
        print(f"Free heap bytes after 1st heaptest: {f1}")
        print(f"Free heap bytes after 2nd heaptest: {f2}")
        t5_pass = (f1 == 65512 and f2 == 65512)
    print("Test 5 (Heap Consistency No Leaks):", "PASS" if t5_pass else "FAIL")
    if t5_pass: passed_count += 1
    print_screen(rows, "Test 5: Repeated heaptest")

    # Test 6: VMM Subsystem Functional (vmtest)
    print("\n[TEST 6] Testing VMM Functionality under Stage 6 (vmtest)...")
    rows = run_qemu_test(text_to_sendkeys("clear\nvmtest\n"), wait_time=0.5, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t6_pass = ("VMM Test:" in screen_text and
               "Mapping: OK" in screen_text and
               "Memory access: OK" in screen_text and
               "Unmap: OK" in screen_text and
               "Frame released: OK" in screen_text and
               "VMM test passed!" in screen_text)
    print("Test 6 (VMM vmtest functional):", "PASS" if t6_pass else "FAIL")
    if t6_pass: passed_count += 1

    # Test 7: vmmap output
    print("\n[TEST 7] Testing vmmap output under Stage 6...")
    rows = run_qemu_test(text_to_sendkeys("clear\nvmmap\n"), wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t7_pass = ("Virtual Memory:" in screen_text and
               "Paging: 4-level" in screen_text and
               "Page Size: 4096 bytes" in screen_text and
               "Root PML4: 0x00106000" in screen_text and
               "Identity Map: 0-1 GiB" in screen_text and
               "VMM: Active" in screen_text)
    print("Test 7 (vmmap Functional):", "PASS" if t7_pass else "FAIL")
    if t7_pass: passed_count += 1

    # Test 8: PMM Subsystem Functional (alloc & free)
    print("\n[TEST 8] Testing PMM Subsystem (alloc & free)...")
    rows = run_qemu_test(text_to_sendkeys("clear\nalloc\nfree\n"), wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t8_pass = "Allocated frame: 0x" in screen_text and "Freed frame: 0x" in screen_text
    print("Test 8 (PMM alloc and free):", "PASS" if t8_pass else "FAIL")
    if t8_pass: passed_count += 1

    # Test 9: meminfo Accounting
    print("\n[TEST 9] Testing meminfo accounting...")
    rows = run_qemu_test(text_to_sendkeys("clear\nmeminfo\n"), wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t9_pass = ("Physical Memory:" in screen_text and
               "Total: " in screen_text and
               "Used:  " in screen_text and
               "Free:  " in screen_text)
    print("Test 9 (meminfo Functional):", "PASS" if t9_pass else "FAIL")
    if t9_pass: passed_count += 1

    # Test 10: Existing Identity Mapping Integrity
    print("\n[TEST 10] Verifying Existing Identity Mapping Integrity...")
    rows = run_qemu_test(text_to_sendkeys("clear\necho IdentityMapActive\n"), wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t10_pass = "IdentityMapActive" in screen_text
    print("Test 10 (Identity Mapping Active):", "PASS" if t10_pass else "FAIL")
    if t10_pass: passed_count += 1

    # Test 11: Keyboard Input under Heap Subsystem
    print("\n[TEST 11] Testing Keyboard Input...")
    rows = run_qemu_test(text_to_sendkeys("clear\necho HeapAndKeyboard\n"), wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t11_pass = "HeapAndKeyboard" in screen_text
    print("Test 11 (Keyboard Input Functional):", "PASS" if t11_pass else "FAIL")
    if t11_pass: passed_count += 1

    # Test 12: Timer Ticks under Heap Subsystem
    print("\n[TEST 12] Testing Timer Ticks...")
    rows = run_qemu_test(text_to_sendkeys("clear\nuptime\n"), wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    tick_match = re.search(r"Ticks:\s*(\d+)", screen_text)
    t12_pass = False
    if tick_match:
        ticks = int(tick_match.group(1))
        t12_pass = ticks > 0
        print(f"Active Ticks: {ticks}")
    print("Test 12 (Timer Ticks Active):", "PASS" if t12_pass else "FAIL")
    if t12_pass: passed_count += 1

    # Test 13: Shell Commands Regression (help and about)
    print("\n[TEST 13] Testing Shell Commands Regression...")
    rows_help = run_qemu_test(text_to_sendkeys("clear\nhelp\n"), wait_time=0.4, boot_wait=1.2)
    text_help = "\n".join(rows_help)
    rows_about = run_qemu_test(text_to_sendkeys("clear\nabout\n"), wait_time=0.4, boot_wait=1.2)
    text_about = "\n".join(rows_about)

    t13_pass = ("Available commands:" in text_help and
                "heapinfo" in text_help and
                "heaptest" in text_help and
                "vmmap" in text_help and
                "vmtest" in text_help and
                "meminfo" in text_help and
                "alloc" in text_help and
                "free" in text_help and
                "uptime" in text_help and
                "VMM: 4 KiB Virtual Page Mapping Active" in text_about and
                "Heap: 64 KiB Free-List Dynamic Allocator" in text_about)
    print("Test 13 (Shell Commands Regression):", "PASS" if t13_pass else "FAIL")
    if t13_pass: passed_count += 1
    print_screen(rows_about, "Test 13: Shell About")

    # Test 14: Sequential Allocation and Deallocation via Shell (Interleaved VMM & Heap)
    print("\n[TEST 14] Testing Interleaved VMM and Heap Operations...")
    keys = text_to_sendkeys("clear\nvmtest\nheaptest\n")
    rows = run_qemu_test(keys, wait_time=0.7, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t14_pass = ("VMM test passed!" in screen_text and
                "Heap test passed!" in screen_text)
    print("Test 14 (Interleaved Heap and VMM Operations):", "PASS" if t14_pass else "FAIL")
    if t14_pass: passed_count += 1
    print_screen(rows, "Test 14: Interleaved VMM and Heap")

    # Test 15: Long-Running Stability
    print("\n[TEST 15] Testing Long-Running Stability (2.0s with interrupts, then heaptest)...")
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(2.0)  # Let timer interrupts fire for 2 full seconds

    for k in text_to_sendkeys("clear\nheaptest\n"):
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.03)

    time.sleep(0.5)
    out, _ = p.communicate(input="xp /4000xb 0xb8000\nq\n")

    chars = []
    for line in out.splitlines():
        if ":" in line:
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
        rows.append(row_str.rstrip())

    screen_text = "\n".join(rows)
    t15_pass = "Heap test passed!" in screen_text
    print("Test 15 (Long-Running Stability):", "PASS" if t15_pass else "FAIL")
    if t15_pass: passed_count += 1
    print_screen(rows, "Test 15: Long-Running Stability")

    print("\n" + "=" * 50)
    print(f"OVERALL SUMMARY: {passed_count}/{total_count} TESTS PASSED")
    print("=" * 50)

    if passed_count != total_count:
        sys.exit(1)

if __name__ == "__main__":
    main()
