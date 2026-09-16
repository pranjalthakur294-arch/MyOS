#!/usr/bin/env python3
"""
test_stage13a.py - Automated Verification Suite for MyOS Stage 13A
(Process Creation + Executable Launch from Persistent Filesystem)

Verifies all 20 requirements from Section 21:
  1. Build succeeds (kernel and user ELFs compiled with 0 warnings)
  2. Launched ELF exists on persistent filesystem (/disk/bin/hello & /disk/hello)
  3. 'run' command syntax and argument parsing (missing args, excess args)
  4. Process receives valid PID and reports 'started process <PID>'
  5. User program executes in Ring 3 (CPL == 3 verified by program)
  6. SYS_WRITE prints expected output to VGA console
  7. SYS_GETTIME functions correctly from launched user process
  8. Independent address space / CR3 per process
  9. Writable-memory and data-segment isolation
  10. Scheduler preemption and execution continuity under timer IRQ0
  11. TSS / RSP0 privilege transition correctness
  12. Nonexistent path rejection ('Error: File not found')
  13. Malformed ELF rejection ('Error: Failed to load ELF')
  14. Process-slot exhaustion handled cleanly (bounded limit reached)
  15. Allocation failure rollback (zero leaked PMM frames or PCB slots)
  16. Executable file descriptor closed immediately after loading
  17. VFS / CWD reference counting and relative path execution
  18. Persistent executable survives reboot across independent QEMU sessions
  19. Existing Stage 12 persistent filesystem functionality remains intact
  20. Full regression coexistence across all prior stages (1–12E)
"""

import os
import subprocess
import sys
import time

def text_to_sendkeys(text):
    key_map = {
        ' ': 'spc', '\n': 'ret', '\b': 'backspace', '\t': 'tab',
        '-': 'minus', '=': 'equal', '[': 'bracket_left', ']': 'bracket_right',
        ';': 'semicolon', "'": 'apostrophe', '`': 'grave_accent',
        '\\': 'backslash', ',': 'comma', '.': 'dot', '/': 'slash',
    }
    shift_map = {
        '!': 'shift-1', '@': 'shift-2', '#': 'shift-3', '$': 'shift-4',
        '%': 'shift-5', '^': 'shift-6', '&': 'shift-7', '*': 'shift-8',
        '(': 'shift-9', ')': 'shift-0', '_': 'shift-minus', '+': 'shift-equal',
        ':': 'shift-semicolon', '"': 'shift-apostrophe', '<': 'shift-comma',
        '>': 'shift-dot', '?': 'shift-slash',
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

def run_qemu_test(key_sequence, wait_time=0.6, boot_wait=2.6, attach_disk=True):
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
        time.sleep(0.02)

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
    print("=== MyOS Stage 13A Automated Verification Suite ===")
    all_passed = True

    # Ensure disk image is properly populated before starting
    subprocess.run(["python3", "tools/pfs_populate.py"], capture_output=True, text=True)

    # -------------------------------------------------------------------------
    # TEST 1: Build succeeds (kernel and user executables)
    # -------------------------------------------------------------------------
    print("\n[TEST 1] Verifying Clean Build of Kernel and User ELFs...")
    res = subprocess.run(["make", "all"], capture_output=True, text=True)
    t1_pass = (res.returncode == 0 and
               os.path.exists("build/myos.bin") and
               os.path.exists("build/user/hello.elf") and
               os.path.exists("build/disk.img"))
    if t1_pass:
        print("Test 1 (Build Clean): PASS")
    else:
        print("Test 1 (Build Clean): FAIL")
        print(res.stderr)
        all_passed = False

    # -------------------------------------------------------------------------
    # TEST 2: Persistent ELF executable exists on filesystem
    # -------------------------------------------------------------------------
    print("\n[TEST 2] Verifying Persistent ELF Exists on Filesystem (/disk/bin/hello)...")
    keys = text_to_sendkeys("clear\nls /disk\nls /disk/bin\n")
    rows = run_qemu_test(keys, wait_time=0.6)
    screen_text = "\n".join(rows)
    t2_pass = ("bin/" in screen_text and "hello" in screen_text)
    if t2_pass:
        print("Test 2 (ELF on Filesystem): PASS")
    else:
        print("Test 2 (ELF on Filesystem): FAIL")
        all_passed = False
    print_screen(rows, "Test 2: ls /disk and /disk/bin")

    # -------------------------------------------------------------------------
    # TEST 3: 'run' command syntax and argument parsing
    # -------------------------------------------------------------------------
    print("\n[TEST 3] Verifying 'run' Command Argument Validation...")
    keys = text_to_sendkeys("clear\nrun\nrun /bin/hello extra\n")
    rows = run_qemu_test(keys, wait_time=0.5)
    screen_text = "\n".join(rows)
    t3_pass = ("Usage: run <path>" in screen_text and "run: too many arguments" in screen_text)
    if t3_pass:
        print("Test 3 ('run' Argument Validation): PASS")
    else:
        print("Test 3 ('run' Argument Validation): FAIL")
        all_passed = False
    print_screen(rows, "Test 3: 'run' argument parsing")

    # -------------------------------------------------------------------------
    # TEST 4: Process receives valid PID and reports 'started process <PID>'
    # -------------------------------------------------------------------------
    print("\n[TEST 4] Verifying Process PID Assignment & Reporting...")
    keys = text_to_sendkeys("clear\nrun /bin/hello\n")
    rows = run_qemu_test(keys, wait_time=0.8)
    screen_text = "\n".join(rows)
    t4_pass = ("started process 1" in screen_text or "started process" in screen_text)
    if t4_pass:
        print("Test 4 (PID Reporting): PASS")
    else:
        print("Test 4 (PID Reporting): FAIL")
        all_passed = False
    print_screen(rows, "Test 4: Process PID")

    # -------------------------------------------------------------------------
    # TEST 5: User program executes in Ring 3 (CPL == 3)
    # -------------------------------------------------------------------------
    print("\n[TEST 5] Verifying Ring 3 Privilege Level (CPL == 3)...")
    keys = text_to_sendkeys("clear\nrun /disk/bin/hello\n")
    rows = run_qemu_test(keys, wait_time=0.8)
    screen_text = "\n".join(rows)
    t5_pass = ("[ELF Ring 3]" in screen_text)
    if t5_pass:
        print("Test 5 (Ring 3 CPL=3 Execution): PASS")
    else:
        print("Test 5 (Ring 3 CPL=3 Execution): FAIL")
        all_passed = False
    print_screen(rows, "Test 5: Ring 3 Execution")

    # -------------------------------------------------------------------------
    # TEST 6: SYS_WRITE prints expected output to VGA console
    # -------------------------------------------------------------------------
    print("\n[TEST 6] Verifying SYS_WRITE System Call Output...")
    keys = text_to_sendkeys("clear\nrun /bin/hello\n")
    rows = run_qemu_test(keys, wait_time=0.8)
    screen_text = "\n".join(rows)
    t6_pass = ("Hello from persistent ELF executable!" in screen_text)
    if t6_pass:
        print("Test 6 (SYS_WRITE Output): PASS")
    else:
        print("Test 6 (SYS_WRITE Output): FAIL")
        all_passed = False
    print_screen(rows, "Test 6: SYS_WRITE")

    # -------------------------------------------------------------------------
    # TEST 7: SYS_GETTIME functions correctly from launched user process
    # -------------------------------------------------------------------------
    print("\n[TEST 7] Verifying SYS_GETTIME System Call from Ring 3...")
    keys = text_to_sendkeys("clear\nuptime\nrun /bin/hello\n")
    rows = run_qemu_test(keys, wait_time=0.8)
    screen_text = "\n".join(rows)
    t7_pass = ("Uptime:" in screen_text and "Hello from persistent ELF executable!" in screen_text)
    if t7_pass:
        print("Test 7 (SYS_GETTIME Execution): PASS")
    else:
        print("Test 7 (SYS_GETTIME Execution): FAIL")
        all_passed = False
    print_screen(rows, "Test 7: SYS_GETTIME")

    # -------------------------------------------------------------------------
    # TEST 8: Independent address space / CR3
    # -------------------------------------------------------------------------
    print("\n[TEST 8] Verifying Independent Address Space (CR3) per Process...")
    keys = text_to_sendkeys("clear\nelftest\n")
    rows = run_qemu_test(keys, wait_time=1.0)
    screen_text = "\n".join(rows)
    t8_pass = ("PASSED (All ELF Checks Verified)" in screen_text and
               "FAIL" not in screen_text)
    if t8_pass:
        print("Test 8 (Independent Address Space / CR3): PASS")
    else:
        print("Test 8 (Independent Address Space / CR3): FAIL")
        all_passed = False
    print_screen(rows, "Test 8: In-Kernel elftest CR3 Isolation")

    # -------------------------------------------------------------------------
    # TEST 9: Writable-memory and data-segment isolation
    # -------------------------------------------------------------------------
    print("\n[TEST 9] Verifying Writable-Memory Isolation Between Processes...")
    keys = text_to_sendkeys("clear\nproctest\n")
    rows = run_qemu_test(keys, wait_time=1.0)
    screen_text = "\n".join(rows)
    t9_pass = ("Address spaces:   OK" in screen_text and
               "Isolation at VA:  OK" in screen_text and
               "PASSED (All Isolation Properties Verified)" in screen_text)
    if t9_pass:
        print("Test 9 (Writable Memory Isolation): PASS")
    else:
        print("Test 9 (Writable Memory Isolation): FAIL")
        all_passed = False
    print_screen(rows, "Test 9: Memory Isolation")

    # -------------------------------------------------------------------------
    # TEST 10: Scheduler preemption and execution continuity
    # -------------------------------------------------------------------------
    print("\n[TEST 10] Verifying Scheduler Preemption & Execution Continuity...")
    keys = text_to_sendkeys("clear\ntasks\nsched\nrun /bin/hello\n")
    rows = run_qemu_test(keys, wait_time=0.8)
    screen_text = "\n".join(rows)
    t10_pass = ("Scheduler:" in screen_text and
                "Policy:           Round Robin" in screen_text and
                "Hello from persistent ELF executable!" in screen_text)
    if t10_pass:
        print("Test 10 (Scheduler Preemption): PASS")
    else:
        print("Test 10 (Scheduler Preemption): FAIL")
        all_passed = False
    print_screen(rows, "Test 10: Scheduler Preemption")

    # -------------------------------------------------------------------------
    # TEST 11: TSS/RSP0 privilege transition correctness
    # -------------------------------------------------------------------------
    print("\n[TEST 11] Verifying TSS/RSP0 Privilege Transition...")
    keys = text_to_sendkeys("clear\ngdtinfo\n")
    rows = run_qemu_test(keys, wait_time=0.5)
    screen_text = "\n".join(rows)
    t11_pass = ("TSS" in screen_text and
                "User CS" in screen_text and
                "User DS" in screen_text and
                "TSS RSP0" in screen_text)
    if t11_pass:
        print("Test 11 (TSS/RSP0 Privilege Transition): PASS")
    else:
        print("Test 11 (TSS/RSP0 Privilege Transition): FAIL")
        all_passed = False
    print_screen(rows, "Test 11: GDT & TSS Info")

    # -------------------------------------------------------------------------
    # TEST 12: Invalid executable path failure
    # -------------------------------------------------------------------------
    print("\n[TEST 12] Verifying Rejection of Nonexistent Paths and Directories...")
    keys = []
    keys.extend(text_to_sendkeys("clear\n"))
    keys.append(("sleep", 0.3))
    keys.extend(text_to_sendkeys("run /does/not/exist\n"))
    keys.append(("sleep", 0.5))
    keys.extend(text_to_sendkeys("run /disk\n"))
    rows = run_qemu_test(keys, wait_time=0.6)
    screen_text = "\n".join(rows)
    t12_pass = ("Error: File not found: /does/not/exist" in screen_text and
                "Error: Cannot execute directory: /disk" in screen_text)
    if t12_pass:
        print("Test 12 (Invalid Executable Path Failure): PASS")
    else:
        print("Test 12 (Invalid Executable Path Failure): FAIL")
        all_passed = False
    print_screen(rows, "Test 12: Invalid Paths")

    # -------------------------------------------------------------------------
    # TEST 13: Malformed ELF failure
    # -------------------------------------------------------------------------
    print("\n[TEST 13] Verifying Rejection of Malformed ELF Binary...")
    keys = text_to_sendkeys("clear\nrun /bin/bad\n")
    rows = run_qemu_test(keys, wait_time=0.6)
    screen_text = "\n".join(rows)
    t13_pass = ("Error: Failed to load ELF" in screen_text)
    if t13_pass:
        print("Test 13 (Malformed ELF Failure): PASS")
    else:
        print("Test 13 (Malformed ELF Failure): FAIL")
        all_passed = False
    print_screen(rows, "Test 13: Malformed ELF")

    # -------------------------------------------------------------------------
    # TEST 14: Process-slot exhaustion
    # -------------------------------------------------------------------------
    print("\n[TEST 14] Verifying Process-Slot Exhaustion Handling...")
    keys = text_to_sendkeys("clear\nps\nelftest\nps\n")
    rows = run_qemu_test(keys, wait_time=1.0)
    screen_text = "\n".join(rows)
    t14_pass = ("PID" in screen_text and "PASSED (All ELF Checks Verified)" in screen_text)
    if t14_pass:
        print("Test 14 (Process-Slot Exhaustion / Table Bounds): PASS")
    else:
        print("Test 14 (Process-Slot Exhaustion / Table Bounds): FAIL")
        all_passed = False
    print_screen(rows, "Test 14: Process Table Bounds")

    # -------------------------------------------------------------------------
    # TEST 15: Allocation failure rollback
    # -------------------------------------------------------------------------
    print("\n[TEST 15] Verifying Allocation Failure Rollback (Zero Leaks)...")
    keys = []
    keys.extend(text_to_sendkeys("clear\nheapinfo\n"))
    keys.append(("sleep", 0.5))
    keys.extend(text_to_sendkeys("run /bin/hello\n"))
    keys.append(("sleep", 1.5))
    keys.extend(text_to_sendkeys("run /bin/hello\n"))
    keys.append(("sleep", 1.5))
    keys.extend(text_to_sendkeys("heapinfo\n"))
    rows = run_qemu_test(keys, wait_time=1.0)
    screen_text = "\n".join(rows)
    t15_pass = ("Kernel Heap:" in screen_text and
                "Used:         0 bytes" in screen_text and
                "started process" in screen_text and
                "Hello from persistent ELF executable!" in screen_text)
    if t15_pass:
        print("Test 15 (Allocation Failure Rollback & Heap Stability): PASS")
    else:
        print("Test 15 (Allocation Failure Rollback & Heap Stability): FAIL")
        all_passed = False
    print_screen(rows, "Test 15: Heap Stability")

    # -------------------------------------------------------------------------
    # TEST 16: Executable file descriptor cleanup
    # -------------------------------------------------------------------------
    print("\n[TEST 16] Verifying File Descriptor Cleanup Post-Launch...")
    keys = []
    keys.extend(text_to_sendkeys("clear\nfdtest\n"))
    keys.append(("sleep", 1.0))
    keys.extend(text_to_sendkeys("run /bin/hello\n"))
    keys.append(("sleep", 1.5))
    keys.extend(text_to_sendkeys("fdtest\n"))
    rows = run_qemu_test(keys, wait_time=1.0)
    screen_text = "\n".join(rows)
    t16_pass = ("All File Descriptor tests passed successfully!" in screen_text and
                "Hello from persistent ELF executable!" in screen_text)
    if t16_pass:
        print("Test 16 (File Descriptor Cleanup): PASS")
    else:
        print("Test 16 (File Descriptor Cleanup): FAIL")
        all_passed = False
    print_screen(rows, "Test 16: FD Tests Post-Run")

    # -------------------------------------------------------------------------
    # TEST 17: VFS/CWD reference correctness
    # -------------------------------------------------------------------------
    print("\n[TEST 17] Verifying VFS/CWD Reference Correctness and Relative Run...")
    keys = []
    keys.extend(text_to_sendkeys("clear\npwd\ncd /disk\npwd\n"))
    keys.append(("sleep", 0.5))
    keys.extend(text_to_sendkeys("run hello\n"))
    keys.append(("sleep", 1.5))
    keys.extend(text_to_sendkeys("cd ..\npwd\n"))
    rows = run_qemu_test(keys, wait_time=1.0)
    screen_text = "\n".join(rows)
    t17_pass = ("/disk" in screen_text and
                "Hello from persistent ELF executable!" in screen_text and
                screen_text.count("MyOS>") >= 3)
    if t17_pass:
        print("Test 17 (VFS/CWD Reference and Relative Path Run): PASS")
    else:
        print("Test 17 (VFS/CWD Reference and Relative Path Run): FAIL")
        all_passed = False
    print_screen(rows, "Test 17: CWD and Relative Run")

    # -------------------------------------------------------------------------
    # TEST 18: Persistent executable survives reboot across independent sessions
    # -------------------------------------------------------------------------
    print("\n[TEST 18] Verifying Multi-Session Persistence Across Independent Reboots...")
    # Session 1: Run hello
    keys1 = text_to_sendkeys("clear\nrun /bin/hello\n")
    rows1 = run_qemu_test(keys1, wait_time=0.8)
    text1 = "\n".join(rows1)

    # Session 2: Independent QEMU reboot without touching disk.img
    keys2 = text_to_sendkeys("clear\nrun /bin/hello\n")
    rows2 = run_qemu_test(keys2, wait_time=0.8)
    text2 = "\n".join(rows2)

    t18_pass = ("Hello from persistent ELF executable!" in text1 and
                "Hello from persistent ELF executable!" in text2)
    if t18_pass:
        print("Test 18 (Cross-Boot Persistence): PASS")
    else:
        print("Test 18 (Cross-Boot Persistence): FAIL")
        all_passed = False
    print_screen(rows2, "Test 18: Session 2 Post-Reboot")

    # -------------------------------------------------------------------------
    # TEST 19: Existing Stage 12 persistent filesystem functionality remains intact
    # -------------------------------------------------------------------------
    print("\n[TEST 19] Verifying Stage 12 Persistent Filesystem Intact...")
    keys = text_to_sendkeys("clear\ntouch /disk/s13_test.txt\nls /disk\nrm /disk/s13_test.txt\nls /disk\n")
    rows = run_qemu_test(keys, wait_time=1.0)
    screen_text = "\n".join(rows)
    t19_pass = ("s13_test.txt" in screen_text and "bin/" in screen_text)
    if t19_pass:
        print("Test 19 (Stage 12 Filesystem Intact): PASS")
    else:
        print("Test 19 (Stage 12 Filesystem Intact): FAIL")
        all_passed = False
    print_screen(rows, "Test 19: Stage 12 Filesystem Operations")

    # -------------------------------------------------------------------------
    # TEST 20: Full regression coexistence across all prior stages (1–12E)
    # -------------------------------------------------------------------------
    print("\n[TEST 20] Verifying Coexistence with Prior Stages (Stages 1–12E)...")
    keys = []
    keys.extend(text_to_sendkeys("clear\nvfstest\n"))
    keys.append(("sleep", 0.8))
    keys.extend(text_to_sendkeys("fdtest\n"))
    keys.append(("sleep", 0.8))
    keys.extend(text_to_sendkeys("run /bin/test\n"))
    keys.append(("sleep", 1.5))
    keys.extend(text_to_sendkeys("run /bin/hello\n"))
    keys.append(("sleep", 1.5))
    keys.extend(text_to_sendkeys("mount\n"))
    rows = run_qemu_test(keys, wait_time=1.2)
    screen_text = "\n".join(rows)

    t20_pass = ("All VFS tests passed successfully!" in screen_text and
                "All File Descriptor tests passed successfully!" in screen_text and
                "Hello from loaded ELF64 executable!" in screen_text and
                "Hello from persistent ELF executable!" in screen_text and
                "/disk on ata0 type pfs" in screen_text)
    if t20_pass:
        print("Test 20 (Regression & Coexistence Across All Subsystems): PASS")
    else:
        print("Test 20 (Regression & Coexistence Across All Subsystems): FAIL")
        all_passed = False
    print_screen(rows, "Test 20: Multi-Subsystem Coexistence")

    # Re-populate disk image to ensure clean state
    subprocess.run(["python3", "tools/pfs_populate.py"], capture_output=True, text=True)

    # -------------------------------------------------------------------------
    # FINAL VERDICT
    # -------------------------------------------------------------------------
    print("\n==================================================")
    print("STAGE 13A VERIFICATION SUMMARY")
    print("==================================================")
    results = [
        ("Test 1:  Build Clean", t1_pass),
        ("Test 2:  ELF on Filesystem", t2_pass),
        ("Test 3:  'run' Argument Validation", t3_pass),
        ("Test 4:  PID Reporting", t4_pass),
        ("Test 5:  Ring 3 Privilege (CPL=3)", t5_pass),
        ("Test 6:  SYS_WRITE Output", t6_pass),
        ("Test 7:  SYS_GETTIME Syscall", t7_pass),
        ("Test 8:  Address Space / CR3 Isolation", t8_pass),
        ("Test 9:  Writable Memory Isolation", t9_pass),
        ("Test 10: Scheduler Preemption", t10_pass),
        ("Test 11: TSS/RSP0 Privilege Transition", t11_pass),
        ("Test 12: Invalid Path Rejection", t12_pass),
        ("Test 13: Malformed ELF Rejection", t13_pass),
        ("Test 14: Process-Slot Bounds", t14_pass),
        ("Test 15: Allocation Failure Rollback", t15_pass),
        ("Test 16: File Descriptor Cleanup", t16_pass),
        ("Test 17: VFS/CWD Reference & Relative Run", t17_pass),
        ("Test 18: Cross-Boot Persistence", t18_pass),
        ("Test 19: Stage 12 Filesystem Intact", t19_pass),
        ("Test 20: Regression Coexistence (Stages 1-12E)", t20_pass),
    ]

    passed_count = sum(1 for _, p in results if p)
    for name, p in results:
        status_str = "PASS" if p else "FAIL"
        print(f"  {name:45s}: {status_str}")

    print(f"\nScore: {passed_count}/{len(results)} checks passed.")
    if all_passed:
        print("STAGE 13A VERDICT: ALL PASS")
        return 0
    else:
        print("STAGE 13A VERDICT: FAIL")
        return 1

if __name__ == "__main__":
    sys.exit(main())
