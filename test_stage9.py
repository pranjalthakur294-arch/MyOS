#!/usr/bin/env python3
"""
test_stage9.py - Automated Verification Suite for MyOS Stage 9 (Process Management Foundation)

Verifies:
  - Boot integrity and 25-row screen line budget (row 0 header, row 24 prompt)
  - Binary symbols for process management subsystem via readelf
  - Shell built-in command 'ps' listing initial kernel process (PID 0)
  - Deterministic PID allocation (PID 1, PID 2) and process bounds
  - Distinct per-process address spaces (proc_a->cr3 != proc_b->cr3 != boot_cr3)
  - Distinct physical frames backing virtual code and stack pages
  - Runtime memory isolation: Process A and Process B write markers to the EXACT SAME
    virtual address (0x60001800), and both values are preserved in separate physical RAM
  - Preemptive round-robin scheduling of Ring 3 user processes under PIT 100 Hz timer
  - System calls (SYS_GETTIME, SYS_WRITE, SYS_EXIT) from user processes
  - Clean process termination via SYS_EXIT with exit status propagation
  - Leak-free memory reclamation: all physical frames returned to PMM
  - Safe PID reuse after process termination and reaping
  - Shell built-in command 'proctest' output and comprehensive verification report
  - Shell built-in commands 'about' and 'help' updated for processes and ps/proctest
  - Post-test shell responsiveness, timer progression, and kernel task continuity
  - Full coexistence with Stage 8A 'usertest' and Stage 8B 'syscalltest'
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
    print("MyOS Stage 9 Automated Verification Test Suite")
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
        print("Boot text dump:")
        for idx, row in enumerate(boot_rows):
            print(f"[{idx:02d}] {row}")

    print_screen(boot_rows, "Test 1: Boot")

    # ----------------------------------------------------
    # TEST 2: Verify binary symbols via readelf
    # ----------------------------------------------------
    print("\n[TEST 2] Verifying Process Binary Symbols via readelf...")
    expected_symbols = [
        "process_init",
        "process_create",
        "process_exit",
        "process_get",
        "process_current",
        "process_reap_terminated",
        "process_print_list",
        "process_run_isolation_test",
        "process_print_test_status",
        "current_process",
        "proc_test_program_a",
        "proc_test_program_b",
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
        print("Test 2 (Process Binary Symbols): PASS")
        passed_tests += 1
    else:
        print(f"Test 2 (Process Binary Symbols): FAIL (Missing: {missing_symbols})")

    # ----------------------------------------------------
    # TEST 3: Shell 'ps' command on fresh boot
    # ----------------------------------------------------
    print("\n[TEST 3] Testing Shell 'ps' Command on Fresh Boot...")
    keys = text_to_sendkeys("ps\n")
    ps_rows = run_qemu_test(keys, wait_time=0.4)
    ps_text = "\n".join(ps_rows)

    t3_pass = ("PID  STATE       TYPE    CR3                 NAME" in ps_text and
               "0    RUNNING     KERNEL" in ps_text and
               "kernel" in ps_text)

    if t3_pass:
        print("Test 3 (Shell 'ps' Command Fresh Boot): PASS")
        passed_tests += 1
    else:
        print("Test 3 (Shell 'ps' Command Fresh Boot): FAIL")

    print_screen(ps_rows, "Test 3: ps command")

    # ----------------------------------------------------
    # TEST 4: Shell 'proctest' command execution
    # ----------------------------------------------------
    print("\n[TEST 4] Testing Shell 'proctest' Command...")
    keys = text_to_sendkeys("proctest\n")
    proctest_rows = run_qemu_test(keys, wait_time=3.5)
    proctest_text = "\n".join(proctest_rows)

    t4_pass = ("Process Isolation & Multitasking Test:" in proctest_text and
               "Address spaces:   OK" in proctest_text and
               "Isolation at VA:  OK" in proctest_text and
               "Preemptive sched: OK" in proctest_text and
               "Syscalls in proc: OK" in proctest_text and
               "Memory reclaim:   OK" in proctest_text and
               "PID reuse:        OK" in proctest_text and
               "PASSED (All Isolation Properties Verified)" in proctest_text)

    if t4_pass:
        print("Test 4 (Shell 'proctest' Command): PASS")
        passed_tests += 1
    else:
        print("Test 4 (Shell 'proctest' Command): FAIL")

    print_screen(proctest_rows, "Test 4: proctest")

    # ----------------------------------------------------
    # TEST 5: Verify SYS_WRITE from both Process A and Process B
    # ----------------------------------------------------
    print("\n[TEST 5] Verifying User-Space SYS_WRITE from Processes...")
    t5_pass = ("[Proc A] Isolation test OK" in proctest_text and
               "[Proc B] Isolation test OK" in proctest_text)

    if t5_pass:
        print("Test 5 (Process SYS_WRITE Output): PASS")
        passed_tests += 1
    else:
        print("Test 5 (Process SYS_WRITE Output): FAIL")

    # ----------------------------------------------------
    # TEST 6: Shell 'ps' command after proctest
    # ----------------------------------------------------
    print("\n[TEST 6] Testing Shell 'ps' Command After Process Termination...")
    keys = text_to_sendkeys("proctest\n") + [("sleep", 3.5)] + text_to_sendkeys("ps\n")
    post_ps_rows = run_qemu_test(keys, wait_time=0.6)
    post_ps_text = "\n".join(post_ps_rows)

    t6_pass = ("PID  STATE       TYPE    CR3                 NAME" in post_ps_text and
               "0    RUNNING     KERNEL" in post_ps_text)

    if t6_pass:
        print("Test 6 (Shell 'ps' Post Termination): PASS")
        passed_tests += 1
    else:
        print("Test 6 (Shell 'ps' Post Termination): FAIL")

    print_screen(post_ps_rows, "Test 6: ps post-termination")

    # ----------------------------------------------------
    # TEST 7: Repeated execution of 'proctest' (stability & 0 leak)
    # ----------------------------------------------------
    print("\n[TEST 7] Testing Repeated Execution of 'proctest'...")
    keys = text_to_sendkeys("proctest\n") + [("sleep", 3.5)] + text_to_sendkeys("proctest\n")
    rep_rows = run_qemu_test(keys, wait_time=3.5)
    rep_text = "\n".join(rep_rows)

    passed_count = len(re.findall(r"PASSED \(All Isolation Properties Verified\)", rep_text))
    t7_pass = (passed_count >= 1 and "FAILED" not in rep_text)

    if t7_pass:
        print(f"Test 7 (Repeated 'proctest' Execution): PASS (Passed {passed_count} times)")
        passed_tests += 1
    else:
        print(f"Test 7 (Repeated 'proctest' Execution): FAIL (Passed count: {passed_count})")

    # ----------------------------------------------------
    # TEST 8: Verify CR3 and page permissions isolation
    # ----------------------------------------------------
    print("\n[TEST 8] Verifying Address Space Isolation Architecture...")
    t8_pass = ("Address spaces:   OK (Distinct CR3 & Physical Frames)" in proctest_text and
               "Code page RO:     OK (PRESENT | USER, WRITABLE=0)" in proctest_text and
               "Stack page RW:    OK (PRESENT | USER | WRITABLE)" in proctest_text and
               "Kernel protection:OK (Supervisor-Only, USER=0)" in proctest_text and
               "Isolation at VA:  OK (Same VA 0x60001800, Separate Data)" in proctest_text)

    if t8_pass:
        print("Test 8 (Address Space Isolation Architecture): PASS")
        passed_tests += 1
    else:
        print("Test 8 (Address Space Isolation Architecture): FAIL")

    # ----------------------------------------------------
    # TEST 9: Shell 'about' command includes Processes
    # ----------------------------------------------------
    print("\n[TEST 9] Verifying Shell 'about' Command Includes Processes...")
    keys = text_to_sendkeys("about\n")
    about_rows = run_qemu_test(keys, wait_time=0.4)
    about_text = "\n".join(about_rows)

    t9_pass = ("Processes: Isolated Address Spaces (CR3) Active" in about_text and
               "Syscalls: int 0x80" in about_text and
               "User Mode: Ring 3 Foundation Active" in about_text)

    if t9_pass:
        print("Test 9 ('about' Command Processes Info): PASS")
        passed_tests += 1
    else:
        print("Test 9 ('about' Command Processes Info): FAIL")

    print_screen(about_rows, "Test 9: about command")

    # ----------------------------------------------------
    # TEST 10: Shell 'help' lists 'ps' and 'proctest'
    # ----------------------------------------------------
    print("\n[TEST 10] Testing Shell 'help' Lists 'ps' and 'proctest'...")
    keys = text_to_sendkeys("help\n")
    help_rows = run_qemu_test(keys, wait_time=0.4)
    help_text = "\n".join(help_rows)

    t10_pass = ("ps" in help_text and
                "proctest" in help_text and
                "Available commands:" in help_text)

    if t10_pass:
        print("Test 10 ('help' Lists ps & proctest): PASS")
        passed_tests += 1
    else:
        print("Test 10 ('help' Lists ps & proctest): FAIL")

    print_screen(help_rows, "Test 10: help command")

    # ----------------------------------------------------
    # TEST 11: Shell responsiveness post proctest
    # ----------------------------------------------------
    print("\n[TEST 11] Verifying Shell Responsiveness Post Process Execution...")
    keys = text_to_sendkeys("proctest\n") + [("sleep", 3.5)] + text_to_sendkeys("echo stage9_rocks\n")
    echo_rows = run_qemu_test(keys, wait_time=0.6)
    echo_text = "\n".join(echo_rows)

    t11_pass = ("stage9_rocks" in echo_text and "MyOS>" in echo_text)

    if t11_pass:
        print("Test 11 (Shell Responsiveness Post Execution): PASS")
        passed_tests += 1
    else:
        print("Test 11 (Shell Responsiveness Post Execution): FAIL")

    # ----------------------------------------------------
    # TEST 12: Scheduler & timer continuity after process execution
    # ----------------------------------------------------
    print("\n[TEST 12] Verifying Scheduler and Timer Continuity After Processes...")
    keys = (text_to_sendkeys("proctest\n") + [("sleep", 3.5)] +
            text_to_sendkeys("uptime\n") + [("sleep", 0.3)] +
            text_to_sendkeys("uptime\n"))
    time_rows = run_qemu_test(keys, wait_time=0.6)
    time_text = "\n".join(time_rows)

    tick_matches = re.findall(r"Ticks:\s+(\d+)", time_text)
    if len(tick_matches) >= 2:
        t1 = int(tick_matches[-2])
        t2 = int(tick_matches[-1])
        t12_pass = (t2 > t1)
        print(f"  Tick progression: {t1} -> {t2}")
    else:
        t12_pass = False

    if t12_pass:
        print("Test 12 (Timer & Scheduler Continuity): PASS")
        passed_tests += 1
    else:
        print("Test 12 (Timer & Scheduler Continuity): FAIL")

    # ----------------------------------------------------
    # TEST 13: Kernel task demo continuity
    # ----------------------------------------------------
    print("\n[TEST 13] Verifying Kernel Task Counters via 'sched'...")
    keys = text_to_sendkeys("proctest\n") + [("sleep", 3.5)] + text_to_sendkeys("sched\n")
    sched_rows = run_qemu_test(keys, wait_time=0.6)
    sched_text = "\n".join(sched_rows)

    match = re.search(r"Counters:\s+A=(\d+)\s+B=(\d+)\s+C=(\d+)", sched_text)
    if match:
        a = int(match.group(1))
        b = int(match.group(2))
        c = int(match.group(3))
        t13_pass = (a > 0 and b > 0 and c > 0)
        print(f"  Kernel task counters: A={a}, B={b}, C={c}")
    else:
        t13_pass = False

    if t13_pass:
        print("Test 13 (Kernel Tasks Continuity): PASS")
        passed_tests += 1
    else:
        print("Test 13 (Kernel Tasks Continuity): FAIL")

    # ----------------------------------------------------
    # TEST 14: Coexistence with Stage 8A 'usertest' and Stage 8B 'syscalltest'
    # ----------------------------------------------------
    print("\n[TEST 14] Verifying Coexistence with usertest and syscalltest...")
    # First verify usertest
    keys_user = text_to_sendkeys("clear\nusertest\n")
    user_rows = run_qemu_test(keys_user, wait_time=0.5)
    user_text = "\n".join(user_rows)

    # Then verify syscalltest followed by proctest
    keys_coexist = (text_to_sendkeys("clear\nsyscalltest\n") + [("sleep", 0.5)] +
                    text_to_sendkeys("proctest\n"))
    coexist_rows = run_qemu_test(keys_coexist, wait_time=3.5)
    coexist_text = "\n".join(coexist_rows)

    t14_pass = ("Verification:     PASSED (Ring 3 Confirmed)" in user_text and
                "Result:           PASSED (All Syscalls Verified)" in coexist_text and
                "Result:           PASSED (All Isolation Properties Verified)" in coexist_text)

    if t14_pass:
        print("Test 14 (Coexistence with usertest & syscalltest): PASS")
        passed_tests += 1
    else:
        print("Test 14 (Coexistence with usertest & syscalltest): FAIL")

    print_screen(coexist_rows, "Test 14: syscalltest + proctest")

    # ----------------------------------------------------
    # Summary
    # ----------------------------------------------------
    print("==================================================")
    print(f"Results: {passed_tests}/{total_tests} tests passed")
    print("==================================================")

    if passed_tests == total_tests:
        print("\nAll Stage 9 verification tests passed successfully!")
        return 0
    else:
        print(f"\nFailure: {total_tests - passed_tests} test(s) failed.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
