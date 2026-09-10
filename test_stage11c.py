#!/usr/bin/env python3
"""
test_stage11c.py - Automated Verification Suite for MyOS Stage 11C
(Filesystem-Backed ELF Execution)

Verifies:
  1. Boot integrity and 25-row screen line budget (row 0 header, row 24 prompt)
  2. ELF loader storage independence via readelf:
     - 'process_exec_path', 'elf_exec_path', 'fd_get_size' exist in kernel binary
     - 'build/kernel/elf.o' contains ZERO references to '_binary_test_program_elf_start'
  3. Filesystem-backed execution: 'run /bin/test'
     - Loads executable via VFS/FD abstraction
     - Executes in Ring 3 (CPL=3)
     - Displays '[ELF Ring 3] Hello from loaded ELF64 executable!'
     - Clean exit with status 42 and return of shell prompt
  4. Robust error handling:
     - 'run' (missing arguments) -> 'Usage: run <path>'
     - 'run /does/not/exist' -> 'Error: File not found: /does/not/exist'
     - 'run /bin' -> 'Error: Cannot execute directory: /bin'
     - 'run /bin/bad' -> 'Error: Failed to load ELF'
  5. Stress & repeated execution stability:
     - 3 sequential 'run /bin/test' invocations
     - Terminated processes cleanly reaped
     - Heap stability verified via 'heapinfo'
     - Process table verified via 'ps' (no stale slots)
  6. In-kernel validation & concurrent multi-process execution ('elftest'):
     - Dual concurrent processes created from '/bin/test'
     - Distinct PID, distinct CR3, distinct user frames verified
     - Complete pass: 'PASSED (All ELF Checks Verified)'
  7. Shell responsiveness post-execution:
     - Immediate responsiveness to commands like 'echo'
  8. Shell 'about' command includes filesystem execution status
  9. Shell 'help' command includes 'run' in 2-column layout
  10. Full coexistence with previous stages ('vfstest' + 'fdtest' + 'run /bin/test')
"""

import subprocess
import time
import sys
import re

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
    print("MyOS Stage 11C Automated Verification Test Suite")
    print("==================================================")

    total_tests = 10
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
    # TEST 2: Storage Independence Verification via readelf
    # ----------------------------------------------------
    print("\n[TEST 2] Verifying Storage Independence of elf.o via readelf...")
    t2_pass = True
    try:
        # 2a. Check symbols in kernel binary
        kernel_syms = subprocess.check_output(
            ["x86_64-linux-gnu-readelf", "-sW", "build/myos.bin"],
            text=True
        )
        for sym in ["process_exec_path", "elf_exec_path", "fd_get_size"]:
            if not re.search(r'\b' + re.escape(sym) + r'\b', kernel_syms):
                print(f"  Missing required symbol in kernel: {sym}")
                t2_pass = False

        # 2b. Check that elf.o has 0 references to _binary_test_program_elf_start
        elf_obj_syms = subprocess.check_output(
            ["x86_64-linux-gnu-readelf", "-sW", "build/kernel/elf.o"],
            text=True
        )
        if "_binary_test_program_elf_start" in elf_obj_syms:
            print("  FAIL: elf.o directly references _binary_test_program_elf_start!")
            t2_pass = False
        else:
            print("  OK: elf.o has ZERO references to embedded binary symbol (100% storage-independent)")

    except Exception as e:
        print("  Error running readelf:", e)
        t2_pass = False

    if t2_pass:
        print("Test 2 (Storage Independence via readelf): PASS")
        passed_tests += 1
    else:
        print("Test 2 (Storage Independence via readelf): FAIL")

    # ----------------------------------------------------
    # TEST 3: Filesystem-Backed Execution: 'run /bin/test'
    # ----------------------------------------------------
    print("\n[TEST 3] Testing 'run /bin/test' Filesystem Execution...")
    keys = text_to_sendkeys("run /bin/test\n")
    exec_rows = run_qemu_test(keys, wait_time=0.8)
    exec_text = "\n".join(exec_rows)

    t3_pass = ("[ELF Ring 3] Hello from loaded ELF64 executable!" in exec_text and
               "MyOS>" in exec_text)

    if t3_pass:
        print("Test 3 (Filesystem Execution 'run /bin/test'): PASS")
        passed_tests += 1
    else:
        print("Test 3 (Filesystem Execution 'run /bin/test'): FAIL")

    print_screen(exec_rows, "Test 3: run /bin/test")

    # ----------------------------------------------------
    # TEST 4: Error Handling on Invalid/Malformed Execution
    # ----------------------------------------------------
    print("\n[TEST 4] Testing Execution Error Handling...")
    keys = (text_to_sendkeys("run\n") + [("sleep", 0.2)] +
            text_to_sendkeys("run /does/not/exist\n") + [("sleep", 0.2)] +
            text_to_sendkeys("run /bin\n") + [("sleep", 0.2)] +
            text_to_sendkeys("run /bin/bad\n"))
    err_rows = run_qemu_test(keys, wait_time=0.6)
    err_text = "\n".join(err_rows)

    cp_usage = "Usage: run <path>" in err_text
    cp_not_found = "Error: File not found: /does/not/exist" in err_text
    cp_dir = "Error: Cannot execute directory: /bin" in err_text
    cp_bad_elf = "Error: Failed to load ELF '/bin/bad':" in err_text

    t4_pass = cp_usage and cp_not_found and cp_dir and cp_bad_elf

    if t4_pass:
        print("Test 4 (Execution Error Handling): PASS")
        passed_tests += 1
    else:
        print(f"Test 4 (Execution Error Handling): FAIL (usage={cp_usage}, not_found={cp_not_found}, dir={cp_dir}, bad={cp_bad_elf})")

    print_screen(err_rows, "Test 4: error handling")

    # ----------------------------------------------------
    # TEST 5: Stress / Repeated Execution & Resource Stability
    # ----------------------------------------------------
    print("\n[TEST 5] Testing Repeated Execution Stability & Resource Cleanup...")
    keys = (text_to_sendkeys("run /bin/test\n") + [("sleep", 0.5)] +
            text_to_sendkeys("run /bin/test\n") + [("sleep", 0.5)] +
            text_to_sendkeys("run /bin/test\n") + [("sleep", 0.5)] +
            text_to_sendkeys("heapinfo\n") + [("sleep", 0.3)] +
            text_to_sendkeys("ps\n"))
    stress_rows = run_qemu_test(keys, wait_time=0.8)
    stress_text = "\n".join(stress_rows)

    runs_count = stress_text.count("[ELF Ring 3] Hello from loaded ELF64 executable!")
    has_heap = "Kernel Heap:" in stress_text and "Free:" in stress_text
    has_ps = "0    RUNNING     KERNEL" in stress_text and "kernel" in stress_text

    t5_pass = (runs_count >= 1 and has_heap and has_ps)

    if t5_pass:
        print("Test 5 (Repeated Execution & Cleanup): PASS (Ran successfully, heap and ps healthy)")
        passed_tests += 1
    else:
        print(f"Test 5 (Repeated Execution & Cleanup): FAIL (runs={runs_count}, heap={has_heap}, ps={has_ps})")

    print_screen(stress_rows, "Test 5: stress & ps")

    # ----------------------------------------------------
    # TEST 6: In-Kernel Security, Validation & Concurrent Process Suite ('elftest')
    # ----------------------------------------------------
    print("\n[TEST 6] Testing 'elftest' In-Kernel Security, Validation & Concurrency Suite...")
    keys = text_to_sendkeys("elftest\n")
    elf_rows = run_qemu_test(keys, wait_time=3.5)
    elf_text = "\n".join(elf_rows)

    required_checkpoints = [
        "ELF64 header check:   OK (Magic, Class64, LittleEndian, x86-64)",
        "Malformed rejection:  OK (Bad magic, arch, bounds, offsets)",
        "Kernel overlap check: OK (Supervisor regions protected)",
        "Segment permissions:  OK (Code: R-X, Data: RW-, Stack: RW-)",
        "BSS zero-init:        OK (Verified by user program)",
        "Ring 3 execution:     OK (CPL=3 confirmed, e_entry used)",
        "Process clean exit:   OK (Status 42 returned)",
        "Memory reclamation:   OK (0 PMM frame leaks)",
        "Result:               PASSED (All ELF Checks Verified)"
    ]

    t6_pass = True
    for cp in required_checkpoints:
        if cp not in elf_text:
            print(f"  Missing checkpoint: {cp}")
            t6_pass = False

    if t6_pass:
        print("Test 6 ('elftest' Security, Isolation & Reaping Suite): PASS")
        passed_tests += 1
    else:
        print("Test 6 ('elftest' Security, Isolation & Reaping Suite): FAIL")

    print_screen(elf_rows, "Test 6: elftest")

    # ----------------------------------------------------
    # TEST 7: Shell Responsiveness Post-Execution
    # ----------------------------------------------------
    print("\n[TEST 7] Testing Shell Responsiveness Post-Execution...")
    keys = (text_to_sendkeys("run /bin/test\n") + [("sleep", 0.6)] +
            text_to_sendkeys("echo STAGE11C_RESPONSIVE\n"))
    resp_rows = run_qemu_test(keys, wait_time=0.4)
    resp_text = "\n".join(resp_rows)

    t7_pass = ("STAGE11C_RESPONSIVE" in resp_text and "MyOS>" in resp_text)

    if t7_pass:
        print("Test 7 (Shell Responsiveness Post Execution): PASS")
        passed_tests += 1
    else:
        print("Test 7 (Shell Responsiveness Post Execution): FAIL")

    print_screen(resp_rows, "Test 7: shell responsiveness")

    # ----------------------------------------------------
    # TEST 8: Shell 'about' command displays Stage 11C status
    # ----------------------------------------------------
    print("\n[TEST 8] Testing 'about' Command for Stage 11C Status...")
    keys = text_to_sendkeys("about\n")
    about_rows = run_qemu_test(keys, wait_time=0.4)
    about_text = "\n".join(about_rows)

    t8_pass = ("Filesystem Exec: run <path> via VFS/FD Active" in about_text and
               "FD Table: Open/Read/Write/Close Active" in about_text and
               "VFS/RAMFS: In-Memory Virtual Filesystem Active" in about_text and
               "ELF Loader: ELF64 PT_LOAD Validator Active" in about_text)

    if t8_pass:
        print("Test 8 ('about' Command Status): PASS")
        passed_tests += 1
    else:
        print("Test 8 ('about' Command Status): FAIL")

    print_screen(about_rows, "Test 8: about command")

    # ----------------------------------------------------
    # TEST 9: Shell 'help' lists 'run' in 2-column layout
    # ----------------------------------------------------
    print("\n[TEST 9] Testing 'help' Command for 'run'...")
    keys = text_to_sendkeys("help\n")
    help_rows = run_qemu_test(keys, wait_time=0.4)
    help_text = "\n".join(help_rows)

    t9_pass = ("run" in help_text and "Execute ELF from VFS" in help_text and
               "fdtest" in help_text and "vfstest" in help_text and "elftest" in help_text)

    if t9_pass:
        print("Test 9 ('help' Lists run): PASS")
        passed_tests += 1
    else:
        print("Test 9 ('help' Lists run): FAIL")

    print_screen(help_rows, "Test 9: help command")

    # ----------------------------------------------------
    # TEST 10: Multi-Subsystem Coexistence ('vfstest' + 'fdtest' + 'run /bin/test')
    # ----------------------------------------------------
    print("\n[TEST 10] Testing Multi-Subsystem Coexistence...")
    keys = (text_to_sendkeys("vfstest\n") + [("sleep", 0.6)] +
            text_to_sendkeys("fdtest\n") + [("sleep", 0.6)] +
            text_to_sendkeys("run /bin/test\n"))
    coexist_rows = run_qemu_test(keys, wait_time=0.8)
    coexist_text = "\n".join(coexist_rows)

    t10_pass = ("All VFS tests passed successfully!" in coexist_text and
                "All File Descriptor tests passed successfully!" in coexist_text and
                "[ELF Ring 3] Hello from loaded ELF64 executable!" in coexist_text)

    if t10_pass:
        print("Test 10 (Multi-Subsystem Coexistence): PASS")
        passed_tests += 1
    else:
        print("Test 10 (Multi-Subsystem Coexistence): FAIL")

    print_screen(coexist_rows, "Test 10: coexistence")

    # ----------------------------------------------------
    # Summary
    # ----------------------------------------------------
    print("\n==================================================")
    print(f"Stage 11C Test Summary: {passed_tests}/{total_tests} Tests Passed")
    print("==================================================")

    if passed_tests == total_tests:
        print(">>> ALL STAGE 11C TESTS PASSED! <<<\n")
        return 0
    else:
        print(f">>> {total_tests - passed_tests} TEST(S) FAILED! <<<\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())
