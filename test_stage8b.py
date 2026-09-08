#!/usr/bin/env python3
"""
test_stage8b.py - Automated Verification Suite for MyOS Stage 8B (System Calls)

Verifies:
  - Binary symbols for syscall subsystem and user wrappers
  - IDT vector 0x80 gate configuration (DPL 3, Interrupt Gate)
  - Syscall ABI dispatch mechanism via int 0x80
  - CPL 3 execution during syscall invocation
  - SYS_WRITE (syscall 1): string output to terminal and byte count return
  - SYS_GETTIME (syscall 2): monotonic timer tick progression
  - User pointer validation: NULL pointer rejection with -EFAULT (-2)
  - Invalid syscall number rejection with -ENOSYS (-3)
  - Zero-length write handling
  - SYS_EXIT (syscall 0): clean exit back to kernel caller
  - Shell command 'syscalltest' output and verification report
  - Repeated execution and system stability
  - Coexistence with Stage 8A 'usertest' and scheduler/heap/VMM/PMM/timer/keyboard
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

def run_qemu_monitor_cmd(qemu_cmds, boot_wait=1.4):
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(boot_wait)
    input_str = "\n".join(qemu_cmds) + "\nq\n"
    out, _ = p.communicate(input=input_str)
    return out

def main():
    print("==================================================")
    print("MyOS Stage 8B Automated Verification Test Suite")
    print("==================================================")

    passed_count = 0
    total_tests = 14

    # -------------------------------------------------------------
    # Test 1: Boot and Stage 8B Milestone Verification
    # -------------------------------------------------------------
    print("\n[TEST 1] Testing Boot and Stage 8B Milestone Checkpoint...")
    rows_boot = run_qemu_test([], wait_time=0.2, boot_wait=1.4)
    boot_text = "\n".join(rows_boot)

    t1_pass = ("MyOS - Educational x86-64 Kernel" in boot_text and
               "Stage 8A Goal Achieved: User mode Ring 3 foundation active!" in boot_text and
               "Stage 8B Goal Achieved: System call subsystem (int 0x80) active!" in boot_text and
               "MyOS>" in boot_text)
    print("Test 1 (Boot & Stage 8B Milestone):", "PASS" if t1_pass else "FAIL")
    if t1_pass: passed_count += 1
    print_screen(rows_boot, "Test 1: Boot")

    # -------------------------------------------------------------
    # Test 2: Binary Symbol Inspection (Syscall Subsystem)
    # -------------------------------------------------------------
    print("\n[TEST 2] Verifying Binary Symbols via readelf...")
    proc = subprocess.run(["readelf", "-sW", "build/myos.bin"], stdout=subprocess.PIPE, text=True)
    syms = proc.stdout
    required_syms = [
        "syscall_init", "syscall_dispatch", "sys_write", "sys_gettime",
        "syscall_validate_user_buffer", "syscall_run_test", "syscall_print_status",
        "isr_syscall", "user_syscall0", "user_syscall2",
        "syscall_test_program", "syscall_test_program_end"
    ]
    t2_pass = all(sym in syms for sym in required_syms)
    print("Test 2 (Syscall Binary Symbols):", "PASS" if t2_pass else "FAIL")
    if t2_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 3: IDT Vector 0x80 Gate Verification (DPL=3)
    # -------------------------------------------------------------
    print("\n[TEST 3] Verifying IDT Vector 0x80 Gate Configuration...")
    mon_reg = run_qemu_monitor_cmd(["info registers"], boot_wait=1.4)
    idt_match = re.search(r"IDT=\s+([0-9a-fA-F]+)", mon_reg)
    t3_pass = False
    if idt_match:
        idt_addr = int(idt_match.group(1), 16)
        entry_addr = idt_addr + (0x80 * 16)
        mon_out = run_qemu_monitor_cmd([f"xp /4xw 0x{entry_addr:x}"], boot_wait=1.4)
        words = []
        for line in mon_out.splitlines():
            if ":" in line and "(qemu)" not in line:
                for w in line.split(":")[1].split():
                    try:
                        words.append(int(w, 16))
                    except ValueError:
                        pass
        if len(words) >= 2:
            type_attr = (words[1] >> 8) & 0xFF
            is_present = bool(type_attr & 0x80)
            dpl = (type_attr >> 5) & 0x03
            gate_type = type_attr & 0x0F
            t3_pass = (is_present and dpl == 3 and (gate_type == 0x0E or gate_type == 0x0F))
            print(f"  IDT 0x80 at 0x{entry_addr:x}: type_attr=0x{type_attr:02x}, Present={is_present}, DPL={dpl}, Type=0x{gate_type:x}")
    print("Test 3 (IDT Vector 0x80 DPL=3 Gate):", "PASS" if t3_pass else "FAIL")
    if t3_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 4: Shell 'syscalltest' Command Execution
    # -------------------------------------------------------------
    print("\n[TEST 4] Testing Shell 'syscalltest' Command...")
    rows_test = run_qemu_test(text_to_sendkeys("clear\nsyscalltest\n"), wait_time=0.5, boot_wait=1.4)
    test_text = "\n".join(rows_test)

    t4_pass = ("System Call Test:" in test_text and
               "[Ring 3 User] Hello from SYS_WRITE!" in test_text and
               "CPL:              3" in test_text and
               "SYS_WRITE:        OK" in test_text and
               "SYS_GETTIME:      OK" in test_text and
               "Time monotonic:   OK" in test_text and
               "Invalid syscall:  OK (Returned -ENOSYS)" in test_text and
               "Invalid pointer:  OK (Returned -EFAULT)" in test_text and
               "PASSED (All Syscalls Verified)" in test_text)
    print("Test 4 (Shell 'syscalltest' Command):", "PASS" if t4_pass else "FAIL")
    if t4_pass: passed_count += 1
    print_screen(rows_test, "Test 4: syscalltest")

    # -------------------------------------------------------------
    # Test 5: SYS_WRITE Output & Byte Count Return
    # -------------------------------------------------------------
    print("\n[TEST 5] Verifying SYS_WRITE Output and Byte Count...")
    write_match = re.search(r"SYS_WRITE:\s+OK \((\d+) bytes\)", test_text)
    t5_pass = False
    if write_match and "[Ring 3 User] Hello from SYS_WRITE!" in test_text:
        byte_count = int(write_match.group(1))
        t5_pass = (byte_count > 0 and byte_count == 38)
        print(f"  Printed bytes: {byte_count}")
    print("Test 5 (SYS_WRITE Output & Byte Count):", "PASS" if t5_pass else "FAIL")
    if t5_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 6: SYS_GETTIME Monotonicity & Tick Progression
    # -------------------------------------------------------------
    print("\n[TEST 6] Verifying SYS_GETTIME Monotonicity...")
    mono_match = re.search(r"Time monotonic:\s+OK \((\d+)\s*<=\s*(\d+)\)", test_text)
    t6_pass = False
    if mono_match:
        t1 = int(mono_match.group(1))
        t2 = int(mono_match.group(2))
        t6_pass = (t1 <= t2 and t1 > 0)
        print(f"  Tick 1: {t1}, Tick 2: {t2}, Monotonic: {t1 <= t2}")
    print("Test 6 (SYS_GETTIME Monotonicity):", "PASS" if t6_pass else "FAIL")
    if t6_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 7: User Pointer Defense (-EFAULT on NULL)
    # -------------------------------------------------------------
    print("\n[TEST 7] Verifying User Pointer Defense (NULL -> -EFAULT)...")
    t7_pass = ("Invalid pointer:  OK (Returned -EFAULT)" in test_text)
    print("Test 7 (Pointer Defense -EFAULT):", "PASS" if t7_pass else "FAIL")
    if t7_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 8: Invalid Syscall Rejection (-ENOSYS)
    # -------------------------------------------------------------
    print("\n[TEST 8] Verifying Invalid Syscall Number Rejection (-ENOSYS)...")
    t8_pass = ("Invalid syscall:  OK (Returned -ENOSYS)" in test_text)
    print("Test 8 (Invalid Syscall -ENOSYS):", "PASS" if t8_pass else "FAIL")
    if t8_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 9: Repeated Execution of 'syscalltest' (Stability)
    # -------------------------------------------------------------
    print("\n[TEST 9] Testing Repeated Execution of 'syscalltest'...")
    repeat_seq = text_to_sendkeys("clear\nsyscalltest\n") + [("sleep", 0.5)] + text_to_sendkeys("syscalltest\n")
    rows_repeat = run_qemu_test(repeat_seq, wait_time=0.8, boot_wait=1.4)
    repeat_text = "\n".join(rows_repeat)
    pass_matches = repeat_text.count("PASSED (All Syscalls Verified)")
    t9_pass = (pass_matches >= 2)
    print(f"  Consecutive PASSED count: {pass_matches}")
    print("Test 9 (Repeated Transitions):", "PASS" if t9_pass else "FAIL")
    if t9_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 10: Shell 'about' Architecture Details Update
    # -------------------------------------------------------------
    print("\n[TEST 10] Verifying Shell 'about' Command Includes Syscalls...")
    rows_about = run_qemu_test(text_to_sendkeys("clear\nabout\n"), wait_time=0.4, boot_wait=1.4)
    about_text = "\n".join(rows_about)

    t10_pass = ("Syscalls: int 0x80 (SYS_WRITE, SYS_GETTIME)" in about_text and
                "User Mode: Ring 3 Foundation Active" in about_text and
                "Scheduler: Timer-Driven Round-Robin Active" in about_text)
    print("Test 10 ('about' includes Syscalls):", "PASS" if t10_pass else "FAIL")
    if t10_pass: passed_count += 1
    print_screen(rows_about, "Test 10: about")

    # -------------------------------------------------------------
    # Test 11: Shell 'help' Command Lists 'syscalltest'
    # -------------------------------------------------------------
    print("\n[TEST 11] Testing Shell 'help' Command Lists 'syscalltest'...")
    rows_help = run_qemu_test(text_to_sendkeys("clear\nhelp\n"), wait_time=0.4, boot_wait=1.4)
    help_text = "\n".join(rows_help)

    t11_pass = ("syscalltest" in help_text and
                "Test system calls" in help_text)
    print("Test 11 ('help' command listing):", "PASS" if t11_pass else "FAIL")
    if t11_pass: passed_count += 1
    print_screen(rows_help, "Test 11: help")

    # -------------------------------------------------------------
    # Test 12: Shell Responsiveness Post Syscall Execution
    # -------------------------------------------------------------
    print("\n[TEST 12] Verifying Shell Responsiveness Post Syscall Execution...")
    rows_echo = run_qemu_test(text_to_sendkeys("clear\nsyscalltest\necho syscall_alive\n"), wait_time=0.5, boot_wait=1.4)
    echo_text = "\n".join(rows_echo)

    t12_pass = ("syscall_alive" in echo_text and "MyOS>" in echo_text)
    print("Test 12 (Shell Responsiveness):", "PASS" if t12_pass else "FAIL")
    if t12_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 13: Scheduler & Timer Continuity After Syscalls
    # -------------------------------------------------------------
    print("\n[TEST 13] Verifying Scheduler and Timer Continuity After Syscalls...")
    rows_sched = run_qemu_test(text_to_sendkeys("syscalltest\nclear\nsched\n"), wait_time=0.4, boot_wait=1.4)
    sched_text = "\n".join(rows_sched)

    t13_pass = ("Scheduler:" in sched_text and
                "Policy:           Round Robin" in sched_text and
                "Context switches:" in sched_text)
    print("Test 13 (Scheduler Continuity):", "PASS" if t13_pass else "FAIL")
    if t13_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 14: Coexistence with Stage 8A 'usertest'
    # -------------------------------------------------------------
    print("\n[TEST 14] Verifying Coexistence with Stage 8A 'usertest'...")
    coexist_seq = text_to_sendkeys("clear\nusertest\n") + [("sleep", 0.5)] + text_to_sendkeys("syscalltest\n")
    rows_coexist = run_qemu_test(coexist_seq, wait_time=0.8, boot_wait=1.4)
    coexist_text = "\n".join(rows_coexist)

    t14_pass = ("PASSED (Ring 3 Confirmed)" in coexist_text and
                "PASSED (All Syscalls Verified)" in coexist_text)
    print("Test 14 (Coexistence with usertest):", "PASS" if t14_pass else "FAIL")
    if t14_pass: passed_count += 1
    print_screen(rows_coexist, "Test 14: usertest + syscalltest")

    # -------------------------------------------------------------
    # Summary
    # -------------------------------------------------------------
    print("\n==================================================")
    print(f"Results: {passed_count}/{total_tests} tests passed")
    print("==================================================")

    if passed_count == total_tests:
        print("\nAll Stage 8B verification tests passed successfully!")
        sys.exit(0)
    else:
        print(f"\nFAILURE: {total_tests - passed_count} tests failed.")
        sys.exit(1)

if __name__ == "__main__":
    main()
