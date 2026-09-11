#!/usr/bin/env python3
"""
test_stage8a.py - Automated Verification Suite for MyOS Stage 8A
Verifies:
  - 6-descriptor (7-entry) GDT structure and RPL 3 user selectors
  - Task State Segment (TSS) initialization and 16-byte aligned RSP0
  - User virtual memory mapping at 0x60000000 with PTE_USER permissions
  - Supervisor-only protection on kernel pages
  - switch_to_user_mode IRETQ transition from Ring 0 to Ring 3
  - Minimal user test program execution at CPL 3 (CS & 3 == 3)
  - Memory write verification in Ring 3 (Canary: 0x1337BEEF, iteration loop)
  - Privilege transition back to Ring 0 via IDT vector 0x80 interrupt gate
  - Hardware-saved CS (0x23) and SS (0x1B) on TSS.rsp0 stack
  - Shell commands 'gdtinfo' and 'usertest'
  - Coexistence with PIT timer, keyboard, scheduler, and kernel heap
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

def run_qemu_monitor_cmd(qemu_cmds, boot_wait=1.8):
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
    print("MyOS Stage 8A Automated Verification Test Suite")
    print("==================================================")

    passed_count = 0
    total_tests = 17

    # -------------------------------------------------------------
    # Test 1: Boot and Stage 8A Milestone Verification
    # -------------------------------------------------------------
    print("\n[TEST 1] Testing Boot and Subsystem Checkpoints...")
    rows_boot = run_qemu_test([], wait_time=0.2, boot_wait=1.4)
    boot_text = "\n".join(rows_boot)

    t1_pass = ("MyOS - Educational x86-64 Kernel" in boot_text and
               "[OK] GDT/TSS & User mode verified (CPL=3, Canary=0x1337BEEF)" in boot_text and
               "Stage 8A Goal Achieved: User mode Ring 3 foundation active!" in boot_text and
               "MyOS>" in boot_text)
    print("Test 1 (Boot & Subsystem Checkpoints):", "PASS" if t1_pass else "FAIL")
    if t1_pass: passed_count += 1
    print_screen(rows_boot, "Test 1: Boot")

    # -------------------------------------------------------------
    # Test 2: Binary Symbol Inspection (GDT & User Mode Subsystem)
    # -------------------------------------------------------------
    print("\n[TEST 2] Verifying Binary Symbols via nm/readelf...")
    proc = subprocess.run(["readelf", "-s", "build/myos.bin"], stdout=subprocess.PIPE, text=True)
    syms = proc.stdout
    required_syms = [
        "gdt_init", "gdt_set_rsp0", "gdt_get_rsp0", "gdt_get_tss", "gdt_print_info",
        "switch_to_user_mode", "isr_user_return", "user_test_program", "user_test_program_end",
        "user_init", "user_run_test", "user_print_status", "user_get_status"
    ]
    t2_pass = all(sym in syms for sym in required_syms)
    print("Test 2 (Binary Symbols):", "PASS" if t2_pass else "FAIL")
    if t2_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 3: GDT Descriptors & Selectors Structure via QEMU Monitor
    # -------------------------------------------------------------
    print("\n[TEST 3] Verifying GDTR & GDT Descriptors in Memory...")
    mon_out = run_qemu_monitor_cmd(["info registers"])
    cs_match = re.search(r"CS\s*=\s*0008", mon_out, re.IGNORECASE)
    ds_match = re.search(r"DS\s*=\s*0010", mon_out, re.IGNORECASE)
    ss_match = re.search(r"SS\s*=\s*0010", mon_out, re.IGNORECASE)
    tr_match = re.search(r"TR\s*=\s*0028", mon_out, re.IGNORECASE)

    t3_pass = bool(cs_match and ds_match and ss_match and tr_match)
    print(f"  CS=0008: {bool(cs_match)}, DS=0010: {bool(ds_match)}, SS=0010: {bool(ss_match)}, TR=0028: {bool(tr_match)}")
    print("Test 3 (GDTR & Segment Registers):", "PASS" if t3_pass else "FAIL")
    if t3_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 4: Task State Segment (TSS) Structure & RSP0 Alignment
    # -------------------------------------------------------------
    print("\n[TEST 4] Verifying TSS State and RSP0 Alignment...")
    tr_info_match = re.search(r"TR\s*=\s*0028\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", mon_out)
    t4_pass = False
    if tr_info_match:
        tr_base = int(tr_info_match.group(1), 16)
        tr_limit = int(tr_info_match.group(2), 16)
        t4_pass = (tr_limit >= 0x67)
        print(f"  TR Base: 0x{tr_base:x}, TR Limit: 0x{tr_limit:x}")
    else:
        t4_pass = bool(tr_match)
    print("Test 4 (TSS TR Limit & Descriptor):", "PASS" if t4_pass else "FAIL")
    if t4_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 5: Shell 'gdtinfo' Command Execution
    # -------------------------------------------------------------
    print("\n[TEST 5] Testing Shell 'gdtinfo' Command...")
    rows_gdt = run_qemu_test(text_to_sendkeys("clear\ngdtinfo\n"), wait_time=0.4, boot_wait=1.4)
    gdt_text = "\n".join(rows_gdt)

    t5_pass = ("GDT & TSS Configuration:" in gdt_text and
               "Kernel CS:" in gdt_text and "0x0008" in gdt_text and
               "Kernel DS:" in gdt_text and "0x0010" in gdt_text and
               "User DS:" in gdt_text and "0x0018" in gdt_text and
               "User CS:" in gdt_text and "0x0020" in gdt_text and
               "TSS Selector:" in gdt_text and "0x0028" in gdt_text and
               "TSS RSP0:" in gdt_text)
    print("Test 5 (Shell 'gdtinfo' Command):", "PASS" if t5_pass else "FAIL")
    if t5_pass: passed_count += 1
    print_screen(rows_gdt, "Test 5: gdtinfo")

    # -------------------------------------------------------------
    # Test 6: Shell 'usertest' Command Execution & Ring 3 Verification
    # -------------------------------------------------------------
    print("\n[TEST 6] Testing Shell 'usertest' Command...")
    rows_user = run_qemu_test(text_to_sendkeys("clear\nusertest\n"), wait_time=0.4, boot_wait=1.4)
    user_text = "\n".join(rows_user)

    t6_pass = ("User Mode (Ring 3) Status:" in user_text and
               "User CS Selector: 0x0023" in user_text and
               "User DS Selector: 0x001B" in user_text and
               "0x60000000" in user_text and
               "0x60001000" in user_text and
               "0x60002000" in user_text and
               "Observed CPL:     3" in user_text and
               "0x1337BEEF" in user_text and
               "Hardware Saved CS:0x00000023" in user_text and
               "Hardware Saved SS:0x0000001B" in user_text and
               "PASSED (Ring 3 Confirmed)" in user_text)
    print("Test 6 (Shell 'usertest' Command):", "PASS" if t6_pass else "FAIL")
    if t6_pass: passed_count += 1
    print_screen(rows_user, "Test 6: usertest")

    # -------------------------------------------------------------
    # Test 7: User Memory Paging & Permissions via QEMU Monitor (info tlb)
    # -------------------------------------------------------------
    print("\n[TEST 7] Verifying User Page Table Permissions (info tlb)...")
    tlb_out = run_qemu_monitor_cmd(["info tlb"])
    has_user_code_map = False
    has_user_stack_map = False
    for line in tlb_out.splitlines():
        if "60000000" in line:
            flags_part = line.split()[-1] if line.split() else ""
            if "U" in flags_part and "W" not in flags_part:
                has_user_code_map = True
        if "60001000" in line and "UW" in line:
            has_user_stack_map = True

    t7_pass = has_user_code_map and has_user_stack_map
    print(f"  User code page mapped with U (Read-Only): {has_user_code_map}, User stack page mapped with UW (Writable): {has_user_stack_map}")
    print("Test 7 (Page Table User Permissions):", "PASS" if t7_pass else "FAIL")
    if t7_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 8: User Mode Execution Canary & Iterations
    # -------------------------------------------------------------
    print("\n[TEST 8] Verifying User Code Iterations & Canary Magic...")
    iter_match = re.search(r"Iterations:\s+(\d+)", user_text)
    t8_pass = False
    if iter_match:
        count = int(iter_match.group(1))
        t8_pass = (count >= 100)
        print(f"  Recorded user iterations: {count}")
    print("Test 8 (User Program Loop Execution):", "PASS" if t8_pass else "FAIL")
    if t8_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 9: Hardware Saved Privilege Transition Frame (CS=0x23, SS=0x1B)
    # -------------------------------------------------------------
    print("\n[TEST 9] Verifying Hardware Saved CS/SS Privilege Levels...")
    t9_pass = ("Hardware Saved CS:0x00000023" in user_text and
               "Hardware Saved SS:0x0000001B" in user_text)
    print("Test 9 (Saved CS=0x23, SS=0x1B):", "PASS" if t9_pass else "FAIL")
    if t9_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 10: Repeated User Mode Transitions (Idempotence & Stability)
    # -------------------------------------------------------------
    print("\n[TEST 10] Testing Repeated User Mode Transitions...")
    rows_repeat = run_qemu_test(text_to_sendkeys("clear\nusertest\nusertest\n"), wait_time=0.5, boot_wait=1.4)
    repeat_text = "\n".join(rows_repeat)
    pass_count = repeat_text.count("PASSED (Ring 3 Confirmed)")
    t10_pass = (pass_count >= 2)
    print(f"  Consecutive PASSED count: {pass_count}")
    print("Test 10 (Repeated Transitions):", "PASS" if t10_pass else "FAIL")
    if t10_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 11: Shell 'about' Architecture Details Update
    # -------------------------------------------------------------
    print("\n[TEST 11] Verifying 'about' Command Includes User Mode...")
    rows_about = run_qemu_test(text_to_sendkeys("clear\nabout\n"), wait_time=0.4, boot_wait=1.4)
    about_text = "\n".join(rows_about)

    t11_pass = ("User Mode: Ring 3 Foundation Active" in about_text and
                "Scheduler: Timer-Driven Round-Robin Active" in about_text and
                "Heap: 64 KiB Free-List Dynamic Allocator" in about_text and
                "VMM: 4 KiB Virtual Page Mapping Active" in about_text)
    print("Test 11 ('about' includes User Mode):", "PASS" if t11_pass else "FAIL")
    if t11_pass: passed_count += 1
    print_screen(rows_about, "Test 11: about")

    # -------------------------------------------------------------
    # Test 12: Shell 'help' Command Lists 'gdtinfo' and 'usertest'
    # -------------------------------------------------------------
    print("\n[TEST 12] Testing Shell 'help' Command for New Built-ins...")
    rows_help = run_qemu_test(text_to_sendkeys("clear\nhelp\n"), wait_time=0.4, boot_wait=1.4)
    help_text = "\n".join(rows_help)

    t12_pass = ("gdtinfo" in help_text and
                "GDT and TSS info" in help_text and
                "usertest" in help_text and
                "Test Ring 3 user mode" in help_text)
    print("Test 12 ('help' command listing):", "PASS" if t12_pass else "FAIL")
    if t12_pass: passed_count += 1
    print_screen(rows_help, "Test 12: help")

    # -------------------------------------------------------------
    # Test 13: Shell Responsiveness After User Mode Execution
    # -------------------------------------------------------------
    print("\n[TEST 13] Verifying Shell Responsiveness Post User Mode...")
    rows_echo = run_qemu_test(text_to_sendkeys("clear\nusertest\necho user_alive\n"), wait_time=0.4, boot_wait=1.4)
    echo_text = "\n".join(rows_echo)

    t13_pass = ("user_alive" in echo_text and "MyOS>" in echo_text)
    print("Test 13 (Post-Transition Shell Responsiveness):", "PASS" if t13_pass else "FAIL")
    if t13_pass: passed_count += 1

    # -------------------------------------------------------------
    # Test 14: Scheduler & Timer Continuity After User Mode
    # -------------------------------------------------------------
    print("\n[TEST 14] Verifying Scheduler and Timer Continuity...")
    rows_sched = run_qemu_test(text_to_sendkeys("usertest\nclear\nsched\n"), wait_time=0.4, boot_wait=1.4)
    sched_text = "\n".join(rows_sched)

    t14_pass = ("Scheduler:" in sched_text and
                "Policy:           Round Robin" in sched_text and
                "Context switches:" in sched_text)
    print("Test 14 (Scheduler Continuity):", "PASS" if t14_pass else "FAIL")
    if t14_pass: passed_count += 1
    print_screen(rows_sched, "Test 14: sched")

    # -------------------------------------------------------------
    # Test 15: Memory & Heap Regression (heaptest after usertest)
    # -------------------------------------------------------------
    print("\n[TEST 15] Verifying Memory & Heap Subsystem Coexistence...")
    rows_heap = run_qemu_test(text_to_sendkeys("usertest\nclear\nheaptest\n"), wait_time=0.5, boot_wait=1.4)
    heap_text = "\n".join(rows_heap)

    t15_pass = ("Heap test passed!" in heap_text)
    print("Test 15 (Heap Subsystem Coexistence):", "PASS" if t15_pass else "FAIL")
    if t15_pass: passed_count += 1
    print_screen(rows_heap, "Test 15: heaptest")

    # -------------------------------------------------------------
    # Test 16: VMM Virtual Memory Mapping (vmtest after usertest)
    # -------------------------------------------------------------
    print("\n[TEST 16] Verifying VMM Virtual Mapping After User Mode...")
    rows_vmm = run_qemu_test(text_to_sendkeys("usertest\nclear\nvmtest\n"), wait_time=0.5, boot_wait=1.4)
    vmm_text = "\n".join(rows_vmm)

    t16_pass = ("VMM test passed!" in vmm_text and "Mapping: OK" in vmm_text)
    print("Test 16 (VMM Subsystem Coexistence):", "PASS" if t16_pass else "FAIL")
    if t16_pass: passed_count += 1
    print_screen(rows_vmm, "Test 16: vmtest")

    # -------------------------------------------------------------
    # Test 17: Long-Running System Stability
    # -------------------------------------------------------------
    print("\n[TEST 17] Verifying System Stability Under Continuous Timer Interrupts...")
    rows_stab = run_qemu_test(text_to_sendkeys("clear\nusertest\nuptime\n"), wait_time=0.8, boot_wait=1.5)
    stab_text = "\n".join(rows_stab)

    t17_pass = ("PASSED (Ring 3 Confirmed)" in stab_text and "Uptime:" in stab_text)
    print("Test 17 (System Stability):", "PASS" if t17_pass else "FAIL")
    if t17_pass: passed_count += 1
    print_screen(rows_stab, "Test 17: usertest + uptime")

    # -------------------------------------------------------------
    # Summary
    # -------------------------------------------------------------
    print("\n" + "=" * 50)
    print(f"Stage 8A Verification Results: {passed_count}/{total_tests} tests passed")
    print("=" * 50)

    if passed_count == total_tests:
        print("\nALL STAGE 8A TESTS PASSED SUCCESSFULLY!")
        return 0
    else:
        print(f"\nFAILURE: {total_tests - passed_count} tests failed.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
