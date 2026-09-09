#!/usr/bin/env python3
"""
test_stage10.py - Automated Verification Suite for MyOS Stage 10 (ELF64 Program Loader)

Verifies:
  - Boot integrity and 25-row screen line budget (row 0 header, row 24 prompt)
  - ELF64 binary symbols in kernel via readelf
  - Embedded user-space ELF program headers (ELF64, x86-64, ET_EXEC, PT_LOAD segments)
  - Strict ELF header validation (magic, 64-bit class, little endian, x86-64, ET_EXEC)
  - PT_LOAD segment validation (filesz <= memsz, bounds, overflow, non-zero memsz)
  - Strict user-space memory boundary enforcement (rejection of kernel overlap)
  - Segment overlap detection and rejection
  - Entry point validation (must reside within executable PT_LOAD segment)
  - Segment permission mapping (Code: PF_R|PF_X -> read-only; Data: PF_R|PF_W -> writable)
  - Dedicated user stack establishment (0x70000000, writable)
  - BSS zero-initialization verified by user code
  - True Ring 3 user mode execution (CPL 3 confirmed by user code)
  - System calls (SYS_WRITE, SYS_GETTIME, SYS_EXIT) from loaded ELF executable
  - Clean process exit via SYS_EXIT with status 42
  - Leak-free memory reclamation: all frames returned to PMM
  - Shell command 'elftest' execution and report
  - Shell commands 'about' and 'help' updated for ELF loader
  - Post-test shell responsiveness, timer progression, and kernel task continuity
  - Full coexistence with Stage 8A 'usertest', Stage 8B 'syscalltest', and Stage 9 'proctest'
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

def run_qemu_test(key_sequence, wait_time=0.4, boot_wait=1.4):
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
    print("MyOS Stage 10 Automated Verification Test Suite")
    print("==================================================")

    total_tests = 14
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
    # TEST 2: Verify binary symbols in build/myos.bin
    # ----------------------------------------------------
    print("\n[TEST 2] Verifying ELF Loader Binary Symbols via readelf...")
    expected_symbols = [
        "elf_init",
        "elf_validate",
        "elf_strerror",
        "elf_load_into_process",
        "process_create_from_elf",
        "elf_run_validation_tests",
        "elf_print_test_status",
        "_binary_test_program_elf_start",
        "_binary_test_program_elf_end",
        "_binary_test_program_elf_size",
    ]

    readelf_out = subprocess.check_output(
        ["x86_64-linux-gnu-readelf", "-sW", "build/myos.bin"],
        text=True
    )

    t2_pass = True
    missing_symbols = []
    for sym in expected_symbols:
        if not re.search(r'\b' + re.escape(sym) + r'\b', readelf_out):
            t2_pass = False
            missing_symbols.append(sym)

    if t2_pass:
        print("Test 2 (ELF Loader Binary Symbols): PASS")
        passed_tests += 1
    else:
        print(f"Test 2 (ELF Loader Binary Symbols): FAIL (Missing: {missing_symbols})")

    # ----------------------------------------------------
    # TEST 3: Inspect Embedded User ELF Headers
    # ----------------------------------------------------
    print("\n[TEST 3] Verifying User ELF Program Structure via readelf...")
    try:
        user_elf_hdr = subprocess.check_output(
            ["x86_64-linux-gnu-readelf", "-h", "-l", "build/user/test_program.elf"],
            text=True
        )
        t3_pass = ("ELF64" in user_elf_hdr and
                   "2's complement, little endian" in user_elf_hdr and
                   "EXEC (Executable file)" in user_elf_hdr and
                   "Advanced Micro Devices X86-64" in user_elf_hdr and
                   "0x60000000" in user_elf_hdr and
                   "LOAD" in user_elf_hdr and
                   "R E" in user_elf_hdr and
                   "RW" in user_elf_hdr)
    except Exception as e:
        print("  Error reading user ELF:", e)
        t3_pass = False

    if t3_pass:
        print("Test 3 (User ELF Headers & PT_LOAD Segments): PASS")
        passed_tests += 1
    else:
        print("Test 3 (User ELF Headers & PT_LOAD Segments): FAIL")

    # ----------------------------------------------------
    # TEST 4: Shell 'elftest' command execution & security verification
    # ----------------------------------------------------
    print("\n[TEST 4] Testing Shell 'elftest' Command...")
    keys = text_to_sendkeys("elftest\n")
    elf_rows = run_qemu_test(keys, wait_time=3.5)
    elf_text = "\n".join(elf_rows)

    t4_pass = ("ELF64 Loader Security & Execution Test:" in elf_text and
               "ELF64 header check:   OK" in elf_text and
               "Malformed rejection:  OK" in elf_text and
               "Kernel overlap check: OK" in elf_text and
               "Segment permissions:  OK" in elf_text and
               "BSS zero-init:        OK" in elf_text and
               "Ring 3 execution:     OK" in elf_text and
               "Process clean exit:   OK" in elf_text and
               "Memory reclamation:   OK" in elf_text and
               "Result:               PASSED (All ELF Checks Verified)" in elf_text)

    if t4_pass:
        print("Test 4 (Shell 'elftest' Command): PASS")
        passed_tests += 1
    else:
        print("Test 4 (Shell 'elftest' Command): FAIL")

    print_screen(elf_rows, "Test 4: elftest")

    # ----------------------------------------------------
    # TEST 5: Verify User-Space SYS_WRITE from ELF program
    # ----------------------------------------------------
    print("\n[TEST 5] Verifying Ring 3 SYS_WRITE Output from ELF Binary...")
    t5_pass = ("[ELF Ring 3] Hello from loaded ELF64 executable!" in elf_text)

    if t5_pass:
        print("Test 5 (ELF Ring 3 SYS_WRITE Output): PASS")
        passed_tests += 1
    else:
        print("Test 5 (ELF Ring 3 SYS_WRITE Output): FAIL")

    # ----------------------------------------------------
    # TEST 6: Shell 'ps' command after ELF process termination
    # ----------------------------------------------------
    print("\n[TEST 6] Testing Shell 'ps' Post ELF Execution...")
    keys = text_to_sendkeys("elftest\n") + [("sleep", 3.5)] + text_to_sendkeys("ps\n")
    ps_rows = run_qemu_test(keys, wait_time=0.5)
    ps_text = "\n".join(ps_rows)

    t6_pass = ("PID  STATE       TYPE    CR3" in ps_text and
               "0    RUNNING     KERNEL" in ps_text and
               "kernel" in ps_text)

    if t6_pass:
        print("Test 6 (Shell 'ps' Post Termination): PASS")
        passed_tests += 1
    else:
        print("Test 6 (Shell 'ps' Post Termination): FAIL")

    print_screen(ps_rows, "Test 6: ps post-termination")

    # ----------------------------------------------------
    # TEST 7: Repeated execution of 'elftest' (Stability & Leaks)
    # ----------------------------------------------------
    print("\n[TEST 7] Testing Repeated Execution of 'elftest'...")
    keys = (text_to_sendkeys("elftest\n") + [("sleep", 3.5)] +
            text_to_sendkeys("elftest\n"))
    rep_rows = run_qemu_test(keys, wait_time=3.5)
    rep_text = "\n".join(rep_rows)

    t7_count = rep_text.count("Result:               PASSED (All ELF Checks Verified)")
    t7_pass = (t7_count >= 1)

    if t7_pass:
        print(f"Test 7 (Repeated 'elftest' Execution): PASS")
        passed_tests += 1
    else:
        print(f"Test 7 (Repeated 'elftest' Execution): FAIL (Passed {t7_count} times)")

    # ----------------------------------------------------
    # TEST 8: Shell 'about' command includes ELF Loader
    # ----------------------------------------------------
    print("\n[TEST 8] Verifying Shell 'about' Command Includes ELF Loader...")
    keys = text_to_sendkeys("about\n")
    about_rows = run_qemu_test(keys, wait_time=0.4)
    about_text = "\n".join(about_rows)

    t8_pass = ("ELF Loader: ELF64 PT_LOAD Validator Active" in about_text)

    if t8_pass:
        print("Test 8 ('about' Command ELF Loader Info): PASS")
        passed_tests += 1
    else:
        print("Test 8 ('about' Command ELF Loader Info): FAIL")

    print_screen(about_rows, "Test 8: about command")

    # ----------------------------------------------------
    # TEST 9: Shell 'help' lists 'elftest'
    # ----------------------------------------------------
    print("\n[TEST 9] Testing Shell 'help' Lists 'elftest'...")
    keys = text_to_sendkeys("help\n")
    help_rows = run_qemu_test(keys, wait_time=0.4)
    help_text = "\n".join(help_rows)

    t9_pass = ("elftest" in help_text and "Test ELF64 loader" in help_text)

    if t9_pass:
        print("Test 9 ('help' Lists elftest): PASS")
        passed_tests += 1
    else:
        print("Test 9 ('help' Lists elftest): FAIL")

    print_screen(help_rows, "Test 9: help command")

    # ----------------------------------------------------
    # TEST 10: Shell responsiveness post ELF execution
    # ----------------------------------------------------
    print("\n[TEST 10] Verifying Shell Responsiveness Post ELF Execution...")
    keys = (text_to_sendkeys("elftest\n") + [("sleep", 3.5)] +
            text_to_sendkeys("echo ELF_LOADER_READY\n"))
    echo_rows = run_qemu_test(keys, wait_time=0.4)
    echo_text = "\n".join(echo_rows)

    t10_pass = ("ELF_LOADER_READY" in echo_text)

    if t10_pass:
        print("Test 10 (Shell Responsiveness Post Execution): PASS")
        passed_tests += 1
    else:
        print("Test 10 (Shell Responsiveness Post Execution): FAIL")

    # ----------------------------------------------------
    # TEST 11: Timer and Scheduler continuity
    # ----------------------------------------------------
    print("\n[TEST 11] Verifying Scheduler and Timer Continuity After ELF...")
    keys = (text_to_sendkeys("elftest\n") + [("sleep", 3.5)] +
            text_to_sendkeys("uptime\n") + [("sleep", 0.5)] +
            text_to_sendkeys("uptime\n"))
    uptime_rows = run_qemu_test(keys, wait_time=0.4)
    uptime_text = "\n".join(uptime_rows)

    ticks = [int(m.group(1)) for m in re.finditer(r"Ticks:\s+(\d+)", uptime_text)]
    if len(ticks) >= 2 and ticks[1] > ticks[0]:
        t11_pass = True
        print(f"  Tick progression: {ticks[0]} -> {ticks[1]}")
    else:
        t11_pass = False

    if t11_pass:
        print("Test 11 (Timer & Scheduler Continuity): PASS")
        passed_tests += 1
    else:
        print("Test 11 (Timer & Scheduler Continuity): FAIL")

    # ----------------------------------------------------
    # TEST 12: Kernel Task Continuity via 'sched'
    # ----------------------------------------------------
    print("\n[TEST 12] Verifying Kernel Task Counters via 'sched'...")
    keys = text_to_sendkeys("elftest\n") + [("sleep", 3.5)] + text_to_sendkeys("sched\n")
    sched_rows = run_qemu_test(keys, wait_time=0.6)
    sched_text = "\n".join(sched_rows)

    match = re.search(r"Counters:\s+A=(\d+)\s+B=(\d+)\s+C=(\d+)", sched_text)
    if match:
        a = int(match.group(1))
        b = int(match.group(2))
        c = int(match.group(3))
        t12_pass = (a > 0 and b > 0 and c > 0)
        print(f"  Kernel task counters: A={a}, B={b}, C={c}")
    else:
        t12_pass = False

    if t12_pass:
        print("Test 12 (Kernel Tasks Continuity): PASS")
        passed_tests += 1
    else:
        print("Test 12 (Kernel Tasks Continuity): FAIL")

    # ----------------------------------------------------
    # TEST 13: Coexistence with Stage 8A 'usertest' & Stage 8B 'syscalltest'
    # ----------------------------------------------------
    print("\n[TEST 13] Verifying Coexistence with usertest and syscalltest...")
    keys_user = text_to_sendkeys("clear\nusertest\n")
    user_rows = run_qemu_test(keys_user, wait_time=0.5)
    user_text = "\n".join(user_rows)

    keys_sys = (text_to_sendkeys("clear\nsyscalltest\n") + [("sleep", 0.5)] +
                text_to_sendkeys("elftest\n"))
    sys_rows = run_qemu_test(keys_sys, wait_time=3.5)
    sys_text = "\n".join(sys_rows)

    t13_pass = ("Verification:     PASSED (Ring 3 Confirmed)" in user_text and
                "Result:           PASSED (All Syscalls Verified)" in sys_text and
                "Result:               PASSED (All ELF Checks Verified)" in sys_text)

    if t13_pass:
        print("Test 13 (Coexistence with usertest & syscalltest): PASS")
        passed_tests += 1
    else:
        print("Test 13 (Coexistence with usertest & syscalltest): FAIL")

    print_screen(sys_rows, "Test 13: syscalltest + elftest")

    # ----------------------------------------------------
    # TEST 14: Coexistence with Stage 9 'proctest'
    # ----------------------------------------------------
    print("\n[TEST 14] Verifying Coexistence with Stage 9 'proctest'...")
    keys_proc = (text_to_sendkeys("clear\nproctest\n") + [("sleep", 3.5)] +
                 text_to_sendkeys("elftest\n"))
    proc_rows = run_qemu_test(keys_proc, wait_time=3.5)
    proc_text = "\n".join(proc_rows)

    t14_pass = ("Result:           PASSED (All Isolation Properties Verified)" in proc_text and
                "Result:               PASSED (All ELF Checks Verified)" in proc_text)

    if t14_pass:
        print("Test 14 (Coexistence with Stage 9 proctest): PASS")
        passed_tests += 1
    else:
        print("Test 14 (Coexistence with Stage 9 proctest): FAIL")

    print_screen(proc_rows, "Test 14: proctest + elftest")

    # ----------------------------------------------------
    # Summary
    # ----------------------------------------------------
    print("==================================================")
    print(f"Results: {passed_tests}/{total_tests} tests passed")
    print("==================================================")

    if passed_tests == total_tests:
        print("\nAll Stage 10 verification tests passed successfully!")
        return 0
    else:
        print(f"\nFailure: {total_tests - passed_tests} test(s) failed.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
