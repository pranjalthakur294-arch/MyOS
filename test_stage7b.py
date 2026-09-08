#!/usr/bin/env python3
"""
test_stage7b.py - Automated Verification Suite for MyOS Stage 7B
Verifies Timer-driven Round-Robin Scheduler, Hardware Preemption, Task Lifecycle,
Task Switching Statistics, Shell Integration ('tasks' and 'sched'),
Heap & Memory Manager Coexistence, and System Stability.
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
    print("STAGE 7B COMPREHENSIVE AUTOMATED VERIFICATION")
    print("==================================================")

    passed_count = 0
    total_count = 22

    # Test 1: Boot to Shell
    print("\n[TEST 1] Verifying Boot to Shell...")
    rows = run_qemu_test([], wait_time=0.3, boot_wait=1.4)
    screen_text = "\n".join(rows)
    t1_pass = "MyOS - Educational x86-64 Kernel" in screen_text and "MyOS>" in screen_text
    print("Test 1 (Boot to Shell):", "PASS" if t1_pass else "FAIL")
    if t1_pass: passed_count += 1
    print_screen(rows, "Test 1: Boot Banner")

    # Test 2: Stage 7B Milestone Initialization Checkpoints
    print("\n[TEST 2] Verifying Stage 7B Initialization Checkpoints...")
    t2_pass = ("[OK] Scheduler initialized" in screen_text and
               "[OK] Timer-driven preemption enabled" in screen_text and
               "[OK] Created Task A" in screen_text and
               "[OK] Created Task B" in screen_text and
               "[OK] Created Task C" in screen_text and
               "Stage 7B Goal Achieved: Timer-driven round-robin scheduler active!" in screen_text)
    print("Test 2 (Stage 7B Milestones):", "PASS" if t2_pass else "FAIL")
    if t2_pass: passed_count += 1

    # Test 3: Round-Robin Demonstration Interleaved Execution [A] [B] [C] [A] [B] [C]
    print("\n[TEST 3] Verifying Round-Robin Preemption Demonstration Sequence...")
    expected_sequence = "[A] [B] [C] [A] [B] [C]"
    t3_pass = "Scheduler demo: [A] [B] [C] [A] [B] [C]" in screen_text
    print("Test 3 (Round-Robin Order):", "PASS" if t3_pass else "FAIL")
    if t3_pass: passed_count += 1

    # Test 4: Preemptive Switching Verification (No Manual Yielding)
    print("\n[TEST 4] Verifying Preemptive Switching (No Manual Yielding)...")
    rows_sched4 = run_qemu_test(text_to_sendkeys("clear\nsched\n"), wait_time=0.4, boot_wait=1.4)
    sched4_text = "\n".join(rows_sched4)
    cnt_match4 = re.search(r'Counters:\s+A=(\d+)\s+B=(\d+)\s+C=(\d+)', sched4_text)
    tick_match4 = re.search(r'Ticks:\s+(\d+)', sched4_text)
    sw_match4 = re.search(r'Context switches:\s+(\d+)', sched4_text)
    t4_pass = ("Scheduler demo:" in screen_text and
               expected_sequence in screen_text and
               cnt_match4 is not None and
               int(cnt_match4.group(1)) > 1000 and
               int(cnt_match4.group(2)) > 1000 and
               int(cnt_match4.group(3)) > 1000 and
               tick_match4 is not None and int(tick_match4.group(1)) > 0 and
               sw_match4 is not None and int(sw_match4.group(1)) > 0)
    print("Test 4 (Preemption Verified):", "PASS" if t4_pass else "FAIL")
    if t4_pass: passed_count += 1

    # Test 5: 'tasks' command displays table header
    print("\n[TEST 5] Verifying 'tasks' command table headers...")
    rows_tasks = run_qemu_test(text_to_sendkeys("clear\ntasks\n"), wait_time=0.4, boot_wait=1.4)
    tasks_text = "\n".join(rows_tasks)
    t5_pass = ("PID" in tasks_text and "Name" in tasks_text and "State" in tasks_text and
               "Stack Base" in tasks_text and "Switches" in tasks_text)
    print("Test 5 ('tasks' Table Headers):", "PASS" if t5_pass else "FAIL")
    if t5_pass: passed_count += 1
    print_screen(rows_tasks, "Test 5: tasks output")

    # Test 6: 'tasks' command displays all 4 task slots (PID 0..3) & task resumption
    print("\n[TEST 6] Verifying all tasks present & resumed across preemptions...")
    t6_pass = ("main" in tasks_text and "task_a" in tasks_text and
               "task_b" in tasks_text and "task_c" in tasks_text and
               cnt_match4 is not None and
               int(cnt_match4.group(1)) > 0 and
               int(cnt_match4.group(2)) > 0 and
               int(cnt_match4.group(3)) > 0)
    print("Test 6 (Tasks Present & Resumed):", "PASS" if t6_pass else "FAIL")
    if t6_pass: passed_count += 1

    # Test 7: 'tasks' command shows Task C in FINISHED state
    print("\n[TEST 7] Verifying Task C finished state...")
    match_c = re.search(r'3\s+task_c\s+(\w+)', tasks_text)
    t7_pass = match_c is not None and match_c.group(1) == "FINISHED"
    print("Test 7 (Task C FINISHED):", "PASS" if t7_pass else "FAIL")
    if t7_pass: passed_count += 1

    # Test 8: 'tasks' command shows Task A and Task B active (READY or RUNNING)
    print("\n[TEST 8] Verifying Task A and Task B active states...")
    match_a = re.search(r'1\s+task_a\s+(\w+)', tasks_text)
    match_b = re.search(r'2\s+task_b\s+(\w+)', tasks_text)
    t8_pass = (match_a is not None and match_a.group(1) in ("READY", "RUNNING") and
               match_b is not None and match_b.group(1) in ("READY", "RUNNING"))
    print("Test 8 (Task A & B Active):", "PASS" if t8_pass else "FAIL")
    if t8_pass: passed_count += 1

    # Test 9: 'tasks' command shows non-zero context switches
    print("\n[TEST 9] Verifying non-zero switch counts in 'tasks' list...")
    match_sw_a = re.search(r'1\s+task_a\s+\w+\s+0x[0-9A-Fa-f]+\s+(\d+)', tasks_text)
    match_sw_b = re.search(r'2\s+task_b\s+\w+\s+0x[0-9A-Fa-f]+\s+(\d+)', tasks_text)
    t9_pass = (match_sw_a is not None and int(match_sw_a.group(1)) > 0 and
               match_sw_b is not None and int(match_sw_b.group(1)) > 0)
    print("Test 9 (Switch Counts Non-Zero):", "PASS" if t9_pass else "FAIL")
    if t9_pass: passed_count += 1

    # Test 10: 'sched' command output fields
    print("\n[TEST 10] Verifying 'sched' command output fields...")
    rows_sched = run_qemu_test(text_to_sendkeys("clear\nsched\n"), wait_time=0.4, boot_wait=1.4)
    sched_text = "\n".join(rows_sched)
    t10_pass = ("Policy:" in sched_text and "Round Robin" in sched_text and
                "Timer:" in sched_text and "100 Hz" in sched_text and
                "Ticks:" in sched_text and "Context switches:" in sched_text and
                "Current task:" in sched_text and "Active tasks:" in sched_text and
                "Counters:" in sched_text and "3" in sched_text)
    print("Test 10 ('sched' Output):", "PASS" if t10_pass else "FAIL")
    if t10_pass: passed_count += 1
    print_screen(rows_sched, "Test 10: sched output")

    # Test 11: Switch count increases over time (two-point differential measurement)
    print("\n[TEST 11] Verifying continuous timer-driven context switches (two-point differential)...")
    keys_double_sched = text_to_sendkeys("clear\nsched\n") + [("sleep", 0.6)] + text_to_sendkeys("sched\n")
    rows_double_sched = run_qemu_test(keys_double_sched, wait_time=0.4, boot_wait=1.4)
    double_sched_text = "\n".join(rows_double_sched)
    switches_matches = re.findall(r'Context switches:\s+(\d+)', double_sched_text)
    ticks_matches = re.findall(r'Ticks:\s+(\d+)', double_sched_text)
    cnts_matches = re.findall(r'Counters:\s+A=(\d+)\s+B=(\d+)\s+C=(\d+)', double_sched_text)
    t11_pass = False
    if len(switches_matches) >= 2:
        s1 = int(switches_matches[0])
        s2 = int(switches_matches[1])
        t11_pass = (s2 > s1 + 10)
        if len(ticks_matches) >= 2:
            t11_pass = t11_pass and (int(ticks_matches[1]) > int(ticks_matches[0]) + 20)
        if len(cnts_matches) >= 2:
            a1, b1, c1 = map(int, cnts_matches[0])
            a2, b2, c2 = map(int, cnts_matches[1])
            t11_pass = t11_pass and (a2 > a1 and b2 > b1 and c2 == c1)
    print(f"Test 11 (Switch Count Growth): {'PASS' if t11_pass else 'FAIL'} (S1={switches_matches[0] if len(switches_matches)>0 else 'N/A'}, S2={switches_matches[1] if len(switches_matches)>1 else 'N/A'})")
    if t11_pass: passed_count += 1

    # Test 12: Shell keyboard responsiveness during background execution
    print("\n[TEST 12] Verifying shell responsiveness during multitasking...")
    rows_echo = run_qemu_test(text_to_sendkeys("clear\necho sched_responsive\n"), wait_time=0.4, boot_wait=1.4)
    echo_text = "\n".join(rows_echo)
    t12_pass = "sched_responsive" in echo_text and "MyOS>" in echo_text
    print("Test 12 (Shell Responsiveness):", "PASS" if t12_pass else "FAIL")
    if t12_pass: passed_count += 1

    # Test 13: 'help' command lists 'tasks' and 'sched'
    print("\n[TEST 13] Verifying 'help' lists 'tasks' and 'sched'...")
    rows_help = run_qemu_test(text_to_sendkeys("clear\nhelp\n"), wait_time=0.4, boot_wait=1.4)
    help_text = "\n".join(rows_help)
    t13_pass = "tasks" in help_text and "sched" in help_text
    print("Test 13 (Help Command):", "PASS" if t13_pass else "FAIL")
    if t13_pass: passed_count += 1

    # Test 14: 'about' command displays Stage 7B information
    print("\n[TEST 14] Verifying 'about' command includes scheduler info...")
    rows_about = run_qemu_test(text_to_sendkeys("clear\nabout\n"), wait_time=0.4, boot_wait=1.4)
    about_text = "\n".join(rows_about)
    t14_pass = ("Round-Robin Scheduler" in about_text or "Preemptive" in about_text or
                "Stage 7B" in about_text or "Scheduler" in about_text)
    print("Test 14 (About Command):", "PASS" if t14_pass else "FAIL")
    if t14_pass: passed_count += 1

    # Test 15: 'uptime' command functional under multitasking
    print("\n[TEST 15] Verifying 'uptime' command...")
    rows_uptime = run_qemu_test(text_to_sendkeys("clear\nuptime\n"), wait_time=0.4, boot_wait=1.4)
    uptime_text = "\n".join(rows_uptime)
    t15_pass = "Uptime:" in uptime_text and "seconds" in uptime_text and "Ticks:" in uptime_text
    print("Test 15 (Uptime Command):", "PASS" if t15_pass else "FAIL")
    if t15_pass: passed_count += 1

    # Test 16: 'meminfo' command functional under multitasking
    print("\n[TEST 16] Verifying 'meminfo' command...")
    rows_mem = run_qemu_test(text_to_sendkeys("clear\nmeminfo\n"), wait_time=0.4, boot_wait=1.4)
    mem_text = "\n".join(rows_mem)
    t16_pass = "Physical Memory:" in mem_text and "Total:" in mem_text and "Free:" in mem_text
    print("Test 16 (Meminfo Command):", "PASS" if t16_pass else "FAIL")
    if t16_pass: passed_count += 1

    # Test 17: 'vmtest' passes under multitasking
    print("\n[TEST 17] Verifying 'vmtest' under multitasking...")
    rows_vm = run_qemu_test(text_to_sendkeys("clear\nvmtest\n"), wait_time=0.5, boot_wait=1.4)
    vm_text = "\n".join(rows_vm)
    t17_pass = "VMM test passed!" in vm_text or "All VMM tests passed successfully" in vm_text
    print("Test 17 (VMM Regression Test):", "PASS" if t17_pass else "FAIL")
    if t17_pass: passed_count += 1

    # Test 18: 'heaptest' passes all 7 tests under multitasking
    print("\n[TEST 18] Verifying 'heaptest' under multitasking...")
    rows_heap = run_qemu_test(text_to_sendkeys("clear\nheaptest\n"), wait_time=0.5, boot_wait=1.4)
    heap_text = "\n".join(rows_heap)
    t18_pass = "Heap test passed!" in heap_text
    print("Test 18 (Heap Regression Test):", "PASS" if t18_pass else "FAIL")
    if t18_pass: passed_count += 1

    # Test 19: Kernel heap integrity preserved (0 used bytes at idle)
    print("\n[TEST 19] Verifying heap integrity preserved (no heap leak from tasks)...")
    rows_heap_stats = run_qemu_test(text_to_sendkeys("clear\nheapinfo\n"), wait_time=0.4, boot_wait=1.4)
    heap_stats_text = "\n".join(rows_heap_stats)
    t19_pass = ("Used:         0 bytes" in heap_stats_text and
                "Free:         65512 bytes" in heap_stats_text and
                "Used Blocks:  0" in heap_stats_text)
    print("Test 19 (Heap Integrity):", "PASS" if t19_pass else "FAIL")
    if t19_pass: passed_count += 1

    # Test 20: Task stack independence
    print("\n[TEST 20] Verifying independent task stack bases...")
    match_bases = re.findall(r'0x([0-9A-Fa-f]{8})', tasks_text)
    unique_bases = set(match_bases)
    t20_pass = len(unique_bases) >= 4
    print("Test 20 (Stack Independence):", "PASS" if t20_pass else "FAIL")
    if t20_pass: passed_count += 1

    # Test 21: Finished task exclusion (Task C skipped by scheduler, switches remain frozen at 3)
    print("\n[TEST 21] Verifying Task C remains excluded from scheduling (switches frozen at 3)...")
    keys_double_tasks = text_to_sendkeys("clear\ntasks\n") + [("sleep", 0.6)] + text_to_sendkeys("tasks\n")
    rows_tasks2 = run_qemu_test(keys_double_tasks, wait_time=0.4, boot_wait=1.4)
    tasks2_text = "\n".join(rows_tasks2)
    c_switches = re.findall(r'3\s+task_c\s+FINISHED\s+0x[0-9A-Fa-f]+\s+(\d+)', tasks2_text)
    a_switches = re.findall(r'1\s+task_a\s+\w+\s+0x[0-9A-Fa-f]+\s+(\d+)', tasks2_text)
    b_switches = re.findall(r'2\s+task_b\s+\w+\s+0x[0-9A-Fa-f]+\s+(\d+)', tasks2_text)
    t21_pass = False
    if len(c_switches) >= 2 and len(a_switches) >= 2 and len(b_switches) >= 2:
        c1, c2 = int(c_switches[0]), int(c_switches[1])
        a1, a2 = int(a_switches[0]), int(a_switches[1])
        b1, b2 = int(b_switches[0]), int(b_switches[1])
        t21_pass = (c1 == 3 and c2 == 3 and a2 > a1 and b2 > b1)
    print(f"Test 21 (Finished Task Exclusion): {'PASS' if t21_pass else 'FAIL'} (Task C switches: {c_switches}, Task A: {a_switches}, Task B: {b_switches})")
    if t21_pass: passed_count += 1

    # Test 22: Stability test (no general protection or page faults during extended run)
    print("\n[TEST 22] Verifying stability under multitasking...")
    rows_stab = run_qemu_test(text_to_sendkeys("clear\nuptime\n"), wait_time=1.0, boot_wait=2.0)
    stab_text = "\n".join(rows_stab)
    t22_pass = "PAGE FAULT" not in stab_text and "Triple fault" not in stab_text and "Uptime:" in stab_text
    print("Test 22 (Multitasking Stability):", "PASS" if t22_pass else "FAIL")
    if t22_pass: passed_count += 1

    print("\n==================================================")
    print(f"STAGE 7B VERIFICATION SUMMARY: {passed_count} / {total_count} PASSED")
    print("==================================================")

    if passed_count == total_count:
        print("[SUCCESS] All Stage 7B tests passed!")
        sys.exit(0)
    else:
        print(f"[FAIL] {total_count - passed_count} tests failed.")
        sys.exit(1)

if __name__ == "__main__":
    main()
