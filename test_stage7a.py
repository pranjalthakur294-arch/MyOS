#!/usr/bin/env python3
"""
test_stage7a.py - Automated Verification Suite for MyOS Stage 7A
Verifies Kernel Task Infrastructure, CPU Context Switching, Independent Stacks,
Task Bootstrapping, Safe Termination, Shell Visibility, and Stability.
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

def run_qemu_test(key_sequence, wait_time=0.4, boot_wait=1.6):
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
    print("STAGE 7A COMPREHENSIVE AUTOMATED VERIFICATION")
    print("==================================================")

    passed_count = 0
    total_count = 15

    # Test 1: Boot to Shell
    print("\n[TEST 1] Verifying Boot to Shell...")
    rows = run_qemu_test([], wait_time=0.3, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t1_pass = "MyOS - Educational x86-64 Kernel" in screen_text and "MyOS>" in screen_text
    print("Test 1 (Boot to Shell):", "PASS" if t1_pass else "FAIL")
    if t1_pass: passed_count += 1
    print_screen(rows, "Test 1: Boot Banner")

    # Test 2: Stage 7A Milestone Banner Checkpoints
    print("\n[TEST 2] Verifying Stage 7A Initialization Checkpoints...")
    t2_pass = ("[OK] Kernel task subsystem initialized" in screen_text and
               "[OK] Task stacks allocated" in screen_text and
               "[OK] Context switching initialized" in screen_text and
               "Stage 7A Goal Achieved: Manual context switching active!" in screen_text)
    print("Test 2 (Stage 7A Milestones):", "PASS" if t2_pass else "FAIL")
    if t2_pass: passed_count += 1

    # Test 3: Task A and Task B Start Messages
    print("\n[TEST 3] Verifying Task A and Task B start messages...")
    t3_pass = "Task A: start" in screen_text and "Task B: start" in screen_text
    print("Test 3 (Task Start Messages):", "PASS" if t3_pass else "FAIL")
    if t3_pass: passed_count += 1

    # Test 4: Task A and Task B Resumed Messages
    print("\n[TEST 4] Verifying Task A and Task B resumed messages...")
    t4_pass = "Task A: resumed" in screen_text and "Task B: resumed" in screen_text
    print("Test 4 (Task Resumed Messages):", "PASS" if t4_pass else "FAIL")
    if t4_pass: passed_count += 1

    # Test 5: Task Safe Completion Messages
    print("\n[TEST 5] Verifying Task A and Task B finished and demo complete...")
    t5_pass = ("Task A: finished" in screen_text and
               "Task B: finished" in screen_text and
               "[OK] Manual context switch test completed" in screen_text)
    print("Test 5 (Task Completion Messages):", "PASS" if t5_pass else "FAIL")
    if t5_pass: passed_count += 1

    # Test 6: Strict Chronological Order of Cooperative Switching
    print("\n[TEST 6] Verifying Strict Chronological Order of Context Switches...")
    idx_a_start   = screen_text.find("Task A: start")
    idx_b_start   = screen_text.find("Task B: start")
    idx_a_resumed = screen_text.find("Task A: resumed")
    idx_b_resumed = screen_text.find("Task B: resumed")
    idx_a_fin     = screen_text.find("Task A: finished")
    idx_b_fin     = screen_text.find("Task B: finished")
    idx_complete  = screen_text.find("[OK] Manual context switch test completed")

    order_ok = (-1 < idx_a_start < idx_b_start < idx_a_resumed <
                idx_b_resumed < idx_a_fin < idx_b_fin < idx_complete)
    print(f"Sequence indices: A_start={idx_a_start}, B_start={idx_b_start}, "
          f"A_resumed={idx_a_resumed}, B_resumed={idx_b_resumed}, "
          f"A_fin={idx_a_fin}, B_fin={idx_b_fin}, complete={idx_complete}")
    print("Test 6 (Strict Execution Order):", "PASS" if order_ok else "FAIL")
    if order_ok: passed_count += 1

    # Test 7: Shell 'tasks' Command Output
    print("\n[TEST 7] Testing 'tasks' shell command...")
    rows = run_qemu_test(text_to_sendkeys("clear\ntasks\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t7_pass = ("Tasks:" in screen_text and
               "PID" in screen_text and
               "Name" in screen_text and
               "State" in screen_text and
               "Stack Base" in screen_text and
               "main" in screen_text and
               "RUNNING" in screen_text and
               "task_a" in screen_text and
               "FINISHED" in screen_text and
               "task_b" in screen_text)
    print("Test 7 (tasks command):", "PASS" if t7_pass else "FAIL")
    if t7_pass: passed_count += 1
    print_screen(rows, "Test 7: tasks command")

    # Test 8: Shell 'tasktest' Command Re-run
    print("\n[TEST 8] Testing 'tasktest' shell command re-run...")
    rows = run_qemu_test(text_to_sendkeys("clear\ntasktest\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t8_pass = ("Task A: start" in screen_text and
               "Task B: start" in screen_text and
               "Task A: resumed" in screen_text and
               "Task B: resumed" in screen_text and
               "Task A: finished" in screen_text and
               "Task B: finished" in screen_text and
               "[OK] Manual context switch test completed" in screen_text)
    print("Test 8 (tasktest command):", "PASS" if t8_pass else "FAIL")
    if t8_pass: passed_count += 1
    print_screen(rows, "Test 8: tasktest command")

    # Test 9: Shell & Terminal Responsiveness after Switching
    print("\n[TEST 9] Testing Shell Responsiveness after context switching...")
    rows = run_qemu_test(text_to_sendkeys("clear\necho Stage7A-Running\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t9_pass = "Stage7A-Running" in screen_text
    print("Test 9 (Shell Responsiveness):", "PASS" if t9_pass else "FAIL")
    if t9_pass: passed_count += 1

    # Test 10: Heap Integrity after Demo Stack Cleanup
    print("\n[TEST 10] Testing Heap Integrity after Task Stack Deallocation...")
    rows = run_qemu_test(text_to_sendkeys("clear\nheapinfo\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t10_pass = ("Kernel Heap:" in screen_text and
                "Start:        0x50000000" in screen_text and
                "Size:         65536 bytes" in screen_text and
                "Used:         0 bytes" in screen_text and
                "Free:         65512 bytes" in screen_text and
                "Blocks:       1" in screen_text and
                "Free Blocks:  1" in screen_text and
                "Used Blocks:  0" in screen_text and
                "Largest Free: 65512 bytes" in screen_text)
    print("Test 10 (Heap Integrity Post-Demo):", "PASS" if t10_pass else "FAIL")
    if t10_pass: passed_count += 1
    print_screen(rows, "Test 10: Heap Post-Demo")

    # Test 11: VMM Functionality under Stage 7A
    print("\n[TEST 11] Testing VMM Functionality under Stage 7A (vmtest)...")
    rows = run_qemu_test(text_to_sendkeys("clear\nvmtest\n"), wait_time=0.5, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t11_pass = ("VMM Test:" in screen_text and
                "Mapping: OK" in screen_text and
                "Memory access: OK" in screen_text and
                "Unmap: OK" in screen_text and
                "Frame released: OK" in screen_text and
                "VMM test passed!" in screen_text)
    print("Test 11 (VMM vmtest functional):", "PASS" if t11_pass else "FAIL")
    if t11_pass: passed_count += 1

    # Test 12: Timer Ticks Active under Multitasking
    print("\n[TEST 12] Testing Timer Ticks Active...")
    rows = run_qemu_test(text_to_sendkeys("clear\nuptime\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    tick_match = re.search(r"Ticks:\s*(\d+)", screen_text)
    t12_pass = False
    if tick_match:
        ticks = int(tick_match.group(1))
        t12_pass = ticks > 0
        print(f"Active Ticks: {ticks}")
    print("Test 12 (Timer Ticks Active):", "PASS" if t12_pass else "FAIL")
    if t12_pass: passed_count += 1

    # Test 13: PS/2 Keyboard Input Functional
    print("\n[TEST 13] Testing Keyboard Input...")
    rows = run_qemu_test(text_to_sendkeys("clear\necho TaskAndKeyboard\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t13_pass = "TaskAndKeyboard" in screen_text
    print("Test 13 (Keyboard Input Functional):", "PASS" if t13_pass else "FAIL")
    if t13_pass: passed_count += 1

    # Test 14: Shell 'about' Architecture Details
    print("\n[TEST 14] Testing Shell 'about' Command...")
    rows = run_qemu_test(text_to_sendkeys("clear\nabout\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t14_pass = ("Tasks: Cooperative Context Switching Active" in screen_text and
                "Architecture: x86-64" in screen_text and
                "Display: VGA 80x25 text buffer" in screen_text)
    print("Test 14 (Shell About Tasks):", "PASS" if t14_pass else "FAIL")
    if t14_pass: passed_count += 1
    print_screen(rows, "Test 14: Shell About")

    # Test 15: Long-Running Stability (continuous interrupts for 2.0s then tasks)
    print("\n[TEST 15] Testing Long-Running Stability...")
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1.0)
    time.sleep(2.0)  # ~200 timer interrupts
    for k in text_to_sendkeys("clear\ntasks\n"):
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.03)
    time.sleep(0.4)
    out, _ = p.communicate(input="xp /4000xb 0xb8000\nq\n")

    chars = []
    for line in out.splitlines():
        if ":" in line:
            for b in line.split(":")[1].split():
                try:
                    chars.append(int(b, 16))
                except ValueError:
                    pass

    stab_rows = []
    for r in range(25):
        row_str = ""
        for c in range(80):
            idx = (r * 80 + c) * 2
            if idx < len(chars):
                byte_val = chars[idx]
                row_str += chr(byte_val) if 32 <= byte_val < 127 else " "
        stab_rows.append(row_str.rstrip())

    stab_text = "\n".join(stab_rows)
    t15_pass = "Tasks:" in stab_text and "RUNNING" in stab_text
    print("Test 15 (Long-Running Stability):", "PASS" if t15_pass else "FAIL")
    if t15_pass: passed_count += 1
    print_screen(stab_rows, "Test 15: Long-Running Stability")

    print("\n" + "=" * 50)
    print(f"OVERALL SUMMARY: {passed_count}/{total_count} TESTS PASSED")
    print("==================================================")

    if passed_count != total_count:
        sys.exit(1)

if __name__ == "__main__":
    main()
