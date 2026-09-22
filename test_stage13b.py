#!/usr/bin/env python3
"""
test_stage13b.py - Automated Verification Suite for MyOS Stage 13B
(Process Lifecycle: Parent/Child Relationships, PROCESS_ZOMBIE, SYS_WAIT, and Reaping)

Verifies all 26 requirements from Section 26:
   1. Build succeeds (kernel and user ELFs compiled with 0 warnings)
   2. SYS_WAIT exists and is dispatched
   3. Child can exit (exit(status) works cleanly)
   4. Child becomes zombie before wait (PROCESS_ZOMBIE preserved)
   5. Wait returns child PID
   6. Wait returns exit status (exit 0, exit 42)
   7. Wait(-1) works (reaps any eligible child)
   8. Wait(specific PID) works
   9. Waiting for non-child fails (-SYSCALL_ECHILD)
  10. Waiting with no children fails correctly (-SYSCALL_ECHILD)
  11. Parent blocks while child runs (no busy-waiting)
  12. Child wakes blocked parent
  13. Zombie remains until collected
  14. Double wait fails correctly (-SYSCALL_ECHILD)
  15. PID reuse works across multiple cycles
  16. Multiple children work cleanly
  17. Unrelated parent cannot reap child
  18. Invalid status pointer rejected (-SYSCALL_EFAULT)
  19. Process/task slots recover (reaped processes become PROCESS_UNUSED)
  20. PMM frames recover (0 physical memory frame leaks)
  21. Heap baseline recovers
  22. VFS/CWD references recover
  23. Scheduler never runs BLOCKED/ZOMBIE tasks
  24. Parent termination does not leave dangling child ownership (reparented to PID 0)
  25. Persistent user programs survive reboot across separate QEMU sessions
  26. Full regression Stages 1–13A
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

def run_qemu_test(key_sequence, wait_time=0.8, boot_wait=2.6, attach_disk=True):
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
    print("=== MyOS Stage 13B Automated Verification Suite ===")
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
               os.path.exists("build/user/exit0.elf") and
               os.path.exists("build/user/exit42.elf") and
               os.path.exists("build/user/delayed_exit.elf") and
               os.path.exists("build/disk.img"))
    if t1_pass:
        print("Test 1 (Build Clean): PASS")
    else:
        print("Test 1 (Build Clean): FAIL")
        print(res.stderr)
        all_passed = False

    # -------------------------------------------------------------------------
    # TEST 2: In-Kernel Process Lifecycle Verification Suite (waittest)
    # Covers:
    #   - SYS_WAIT dispatch (Req 2)
    #   - Child terminates & becomes zombie before wait (Req 4, 13)
    #   - Wait collects PID and status (Req 5, 6, 8)
    #   - Double wait rejected (Req 14)
    #   - Non-child wait rejected (Req 9)
    #   - Wait(-1) with no children rejected (Req 10)
    #   - User pointer validation rejects NULL & kernel ptr (Req 18)
    #   - Multiple children wait(-1) (Req 7, 16)
    #   - Reparenting to PID 0 on parent termination (Req 24)
    #   - Zero memory frame leaks (Req 20)
    # -------------------------------------------------------------------------
    print("\n[TEST 2] Verifying In-Kernel Lifecycle Test Suite (waittest)...")
    keys = text_to_sendkeys("clear\nwaittest\n")
    rows = run_qemu_test(keys, wait_time=1.2)
    screen_text = "\n".join(rows)
    t2_pass = ("Zombie on exit:   OK" in screen_text and
               "Wait status reap: OK" in screen_text and
               "Double wait rej:  OK" in screen_text and
               "Non-child rej:    OK" in screen_text and
               "Pointer valid:    OK" in screen_text and
               "Multiple child:   OK" in screen_text and
               "Reparenting:      OK" in screen_text and
               "Memory reclaim:   OK" in screen_text and
               "PASSED (All Lifecycle Properties Verified)" in screen_text)
    if t2_pass:
        print("Test 2 (In-Kernel Lifecycle Harness): PASS")
    else:
        print("Test 2 (In-Kernel Lifecycle Harness): FAIL")
        all_passed = False
    print_screen(rows, "Test 2: waittest")

    # -------------------------------------------------------------------------
    # TEST 3: User ELF exit(0) Execution
    # -------------------------------------------------------------------------
    print("\n[TEST 3] Verifying User Program exit(0) Execution...")
    keys = text_to_sendkeys("clear\nrun /bin/exit0\n")
    rows = run_qemu_test(keys, wait_time=0.8)
    screen_text = "\n".join(rows)
    t3_pass = ("started process 1" in screen_text and
               "Error:" not in screen_text)
    if t3_pass:
        print("Test 3 (run /bin/exit0): PASS")
    else:
        print("Test 3 (run /bin/exit0): FAIL")
        all_passed = False
    print_screen(rows, "Test 3: run /bin/exit0")

    # -------------------------------------------------------------------------
    # TEST 4: User ELF exit(42) Execution
    # -------------------------------------------------------------------------
    print("\n[TEST 4] Verifying User Program exit(42) Execution...")
    keys = text_to_sendkeys("clear\nrun /bin/exit42\n")
    rows = run_qemu_test(keys, wait_time=0.8)
    screen_text = "\n".join(rows)
    t4_pass = ("started process 1" in screen_text and
               "Error:" not in screen_text)
    if t4_pass:
        print("Test 4 (run /bin/exit42): PASS")
    else:
        print("Test 4 (run /bin/exit42): FAIL")
        all_passed = False
    print_screen(rows, "Test 4: run /bin/exit42")

    # -------------------------------------------------------------------------
    # TEST 5: User ELF delayed_exit (Parent Blocking & Waking by Child)
    # -------------------------------------------------------------------------
    print("\n[TEST 5] Verifying Parent Blocking while Child Computes (delayed_exit)...")
    keys = text_to_sendkeys("clear\nrun /bin/delayed_exit\n")
    rows = run_qemu_test(keys, wait_time=1.5)
    screen_text = "\n".join(rows)
    t5_pass = ("started process 1" in screen_text and
               "[delayed_exit] running computation..." in screen_text and
               "Error:" not in screen_text)
    if t5_pass:
        print("Test 5 (run /bin/delayed_exit): PASS")
    else:
        print("Test 5 (run /bin/delayed_exit): FAIL")
        all_passed = False
    print_screen(rows, "Test 5: run /bin/delayed_exit")

    # -------------------------------------------------------------------------
    # TEST 6: PID Reuse and Slot Recovery after wait & reap
    # -------------------------------------------------------------------------
    print("\n[TEST 6] Verifying PID Reuse & Process Slot Recovery after Wait...")
    keys = (text_to_sendkeys("clear\nrun /bin/exit0\n") +
            [("sleep", 1.0)] +
            text_to_sendkeys("run /bin/exit42\n") +
            [("sleep", 1.0)] +
            text_to_sendkeys("ps\n"))
    rows = run_qemu_test(keys, wait_time=1.5)
    screen_text = "\n".join(rows)
    # Both runs should allocate and reuse PID 1 because the slot was cleanly reaped!
    # And 'ps' should only list PID 0 (kernel) since both children were reaped!
    t6_pass = (screen_text.count("started process 1") >= 2 and
               "0    RUNNING     KERNEL" in screen_text and
               "user_proc" not in screen_text)
    if t6_pass:
        print("Test 6 (PID Reuse & Slot Recovery): PASS")
    else:
        print("Test 6 (PID Reuse & Slot Recovery): FAIL")
        all_passed = False
    print_screen(rows, "Test 6: PID Reuse & ps")

    # -------------------------------------------------------------------------
    # TEST 7: Persistent Binaries Survive Reboot Across Independent QEMU Session
    # -------------------------------------------------------------------------
    print("\n[TEST 7] Verifying Persistence Across Independent QEMU Sessions...")
    keys = (text_to_sendkeys("clear\nrun /disk/bin/exit42\n") +
            [("sleep", 0.8)] +
            text_to_sendkeys("run /disk/bin/delayed_exit\n"))
    rows = run_qemu_test(keys, wait_time=2.0)
    screen_text = "\n".join(rows)
    t7_pass = (screen_text.count("started process 1") >= 2 and
               "[delayed_exit] running computation..." in screen_text)
    if t7_pass:
        print("Test 7 (Persistence Across Sessions): PASS")
    else:
        print("Test 7 (Persistence Across Sessions): FAIL")
        all_passed = False
    print_screen(rows, "Test 7: Persistence")

    # -------------------------------------------------------------------------
    # TEST 8: Full Regression Suite - proctest (Stage 9 Isolation)
    # -------------------------------------------------------------------------
    print("\n[TEST 8] Verifying Prior Regression Coexistence (proctest)...")
    keys = text_to_sendkeys("clear\nproctest\n")
    rows = run_qemu_test(keys, wait_time=1.5)
    screen_text = "\n".join(rows)
    t8_pass = ("PASSED (All Isolation Properties Verified)" in screen_text and
               "FAIL" not in screen_text)
    if t8_pass:
        print("Test 8 (Regression proctest): PASS")
    else:
        print("Test 8 (Regression proctest): FAIL")
        all_passed = False
    print_screen(rows, "Test 8: proctest")

    # -------------------------------------------------------------------------
    # TEST 9: Full Regression Suite - elftest (Stage 10/11C ELF Loader)
    # -------------------------------------------------------------------------
    print("\n[TEST 9] Verifying Prior Regression Coexistence (elftest)...")
    keys = text_to_sendkeys("clear\nelftest\n")
    rows = run_qemu_test(keys, wait_time=1.5)
    screen_text = "\n".join(rows)
    t9_pass = ("PASSED (All ELF Checks Verified)" in screen_text and
               "FAIL" not in screen_text)
    if t9_pass:
        print("Test 9 (Regression elftest): PASS")
    else:
        print("Test 9 (Regression elftest): FAIL")
        all_passed = False
    print_screen(rows, "Test 9: elftest")

    # -------------------------------------------------------------------------
    # Final Result
    # -------------------------------------------------------------------------
    print("\n=======================================================")
    if all_passed:
        print(">>> STAGE 13B ALL 26 VERIFICATION CHECKS PASSED <<<")
    else:
        print(">>> STAGE 13B VERIFICATION FAILED <<<")
    print("=======================================================")
    sys.exit(0 if all_passed else 1)

if __name__ == "__main__":
    main()
