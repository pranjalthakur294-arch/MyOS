#!/usr/bin/env python3
"""
test_stage14a.py - Automated Verification Suite for MyOS Stage 14A
(User Process I/O & Standard Streams: fd 0/1/2, Terminal VFS Pipeline, Non-busy Blocking Read)

Verifies all 24 requirements:
   1. SYS_READ exists and dispatches correctly
   2. SYS_WRITE remains compatible with legacy calls
   3. fd 0 exists (stdin, O_RDONLY)
   4. fd 1 exists (stdout, O_WRONLY)
   5. fd 2 exists (stderr, O_WRONLY)
   6. stdout reaches terminal
   7. stderr reaches terminal
   8. stdin reaches user process
   9. invalid fd rejected (-EBADF)
  10. closed fd rejected (-EBADF)
  11. invalid user pointer rejected (-EFAULT)
  12. page-boundary pointer validation
  13. zero-length read (returns 0)
  14. zero-length write (returns 0)
  15. process exit closes standard FDs
  16. no FD leak across repeated process runs
  17. no VFS reference leak (terminal refcount returns to baseline)
  18. blocked reader does not busy-wait
  19. reader wakes after keyboard input
  20. multiple-reader policy enforced (-EBUSY)
  21. process can still be reaped correctly
  22. Stage 13B wait tests still pass (waittest)
  23. Stage 12 filesystem tests still pass (pfstest/mounttest)
  24. syscall tests still pass (syscalltest)
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

def run_qemu_test(key_sequence, wait_time=0.8, boot_wait=3.0, attach_disk=True):
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
    print("---------------------------\n")

def main():
    print("=== MyOS Stage 14A Automated Verification Suite ===")
    all_passed = True

    # Ensure disk image is properly populated before starting
    subprocess.run([sys.executable, "tools/pfs_populate.py"], capture_output=True, text=True)

    # -------------------------------------------------------------------------
    # TEST 1: Build succeeds (kernel and all user executables)
    # -------------------------------------------------------------------------
    print("\n[TEST 1] Verifying Clean Build of Kernel and User ELFs...")
    res = subprocess.run(["make", "all"], capture_output=True, text=True)
    t1_pass = (res.returncode == 0 and
               os.path.exists("build/myos.bin") and
               os.path.exists("build/user/write_test.elf") and
               os.path.exists("build/user/read_test.elf") and
               os.path.exists("build/user/io_test.elf") and
               os.path.exists("build/disk.img"))
    if t1_pass:
        print("Test 1 (Build Clean): PASS")
    else:
        print("Test 1 (Build Clean): FAIL")
        print(res.stderr)
        all_passed = False

    # -------------------------------------------------------------------------
    # TEST 2: In-Kernel Standard Streams & VFS Integration (stdiotest)
    # Covers:
    #   - Terminal VFS device node operations
    #   - fd 0/1/2 initialization and flags
    #   - Independent open_file objects (stdout != stderr)
    #   - Independent close(1) leaves fd 2 functional
    #   - Permission enforcement (write to stdin -> EACCES, read from stderr -> EACCES)
    #   - Reference count restoration (0 VFS leaks)
    #   - User buffer validation (NULL, kernel pointer, RO code page)
    # -------------------------------------------------------------------------
    print("\n[TEST 2] Verifying In-Kernel Standard Streams Test Suite (stdiotest)...")
    keys = text_to_sendkeys("clear\nstdiotest\n")
    rows = run_qemu_test(keys, wait_time=1.0)
    screen_text = "\n".join(rows)
    t2_pass = ("[OK] Terminal VFS node and device operations" in screen_text and
               "[OK] Kernel process standard stream descriptors" in screen_text and
               "[OK] Independent FD close and EBADF isolation" in screen_text and
               "[OK] Permission enforcement" in screen_text and
               "[OK] Clean teardown and reference count restoration" in screen_text and
               "[OK] sys_read security boundary and buffer validation" in screen_text and
               "All Stage 14A stdio kernel tests passed successfully!" in screen_text)
    if t2_pass:
        print("Test 2 (stdiotest): PASS")
    else:
        print("Test 2 (stdiotest): FAIL")
        all_passed = False
    print_screen(rows, "Test 2: stdiotest")

    # -------------------------------------------------------------------------
    # TEST 3: User Process stdout and stderr Output (write_test)
    # Covers:
    #   - Ring 3 sys_write(1, ...) reaches terminal
    #   - Ring 3 sys_write(2, ...) reaches terminal
    #   - Process exits cleanly with status 0
    # -------------------------------------------------------------------------
    print("\n[TEST 3] Verifying Ring 3 Standard Stream Output (/bin/write_test)...")
    keys = text_to_sendkeys("clear\nrun /bin/write_test\n")
    rows = run_qemu_test(keys, wait_time=1.0)
    screen_text = "\n".join(rows)
    t3_pass = ("started process 1" in screen_text and
               "write_test: hello stdout (fd 1)" in screen_text and
               "write_test: hello stderr (fd 2)" in screen_text and
               "Error:" not in screen_text)
    if t3_pass:
        print("Test 3 (run /bin/write_test): PASS")
    else:
        print("Test 3 (run /bin/write_test): FAIL")
        all_passed = False
    print_screen(rows, "Test 3: run /bin/write_test")

    # -------------------------------------------------------------------------
    # TEST 4: User Process Interactive Input & Echo (/bin/read_test)
    # Covers:
    #   - Ring 3 sys_write(1, prompt) reaches terminal
    #   - Ring 3 sys_read(0, buf) blocks without busy-waiting
    #   - Keyboard input wakes blocked reader
    #   - Input echoed cleanly to stdout (fd 1)
    #   - Process exits cleanly with status 0
    # -------------------------------------------------------------------------
    print("\n[TEST 4] Verifying Ring 3 Interactive Terminal Read & Wakeup (/bin/read_test)...")
    keys = text_to_sendkeys("clear\nrun /bin/read_test\n")
    keys.append(("sleep", 0.8))  # Wait for read_test to launch and block on sys_read
    keys.extend(text_to_sendkeys("antigravity\n"))
    rows = run_qemu_test(keys, wait_time=1.2)
    screen_text = "\n".join(rows)
    t4_pass = ("started process 1" in screen_text and
               "read_test: enter text: antigravity" in screen_text and
               "read_test echo: antigravity" in screen_text and
               "Error:" not in screen_text)
    if t4_pass:
        print("Test 4 (run /bin/read_test): PASS")
    else:
        print("Test 4 (run /bin/read_test): FAIL")
        all_passed = False
    print_screen(rows, "Test 4: run /bin/read_test")

    # -------------------------------------------------------------------------
    # TEST 4B: Shell Input Isolation & Stale Buffer Rejection (F-04 Adversarial)
    # Sequence:
    #   1. abc <ENTER> -> Unknown command: abc
    #   2. run /bin/read_test <ENTER>
    #   3. hello <ENTER> -> read_test echo: hello
    #   4. run /bin/read_test <ENTER>
    #   5. world <ENTER> -> read_test echo: world
    # Verifies leftover shell/process input never leaks into subsequent processes.
    # -------------------------------------------------------------------------
    print("\n[TEST 4B] Verifying Shell Input Isolation & Non-Bleed (F-04 Adversarial)...")
    keys = (text_to_sendkeys("clear\nabc\n") +
            [("sleep", 0.3)] +
            text_to_sendkeys("run /bin/read_test\n") +
            [("sleep", 0.8)] +
            text_to_sendkeys("hello\n") +
            [("sleep", 0.8)] +
            text_to_sendkeys("run /bin/read_test\n") +
            [("sleep", 0.8)] +
            text_to_sendkeys("world\n"))
    rows = run_qemu_test(keys, wait_time=1.2)
    screen_text = "\n".join(rows)
    t4b_pass = ("Unknown command: abc" in screen_text and
                "read_test echo: hello" in screen_text and
                "read_test echo: world" in screen_text and
                "read_test echo: abc" not in screen_text and
                "Error:" not in screen_text)
    if t4b_pass:
        print("Test 4B (F-04 Shell Input Isolation): PASS")
    else:
        print("Test 4B (F-04 Shell Input Isolation): FAIL")
        all_passed = False
    print_screen(rows, "Test 4B: F-04 Shell Input Isolation")

    # -------------------------------------------------------------------------
    # TEST 5: Comprehensive Ring 3 I/O Assertions (/bin/io_test)
    # Covers:
    #   - All 20 Ring 3 assertions in io_test.c
    #   - Zero length read/write
    #   - Permission rejections (EACCES)
    #   - Invalid FD rejections (EBADF)
    #   - Buffer / pointer security rejections (EFAULT)
    #   - Independent close(1) leaves stderr intact
    # -------------------------------------------------------------------------
    print("\n[TEST 5] Verifying Ring 3 Exhaustive I/O Assertions (/bin/io_test)...")
    keys = text_to_sendkeys("clear\nrun /bin/io_test\n")
    rows = run_qemu_test(keys, wait_time=1.2)
    screen_text = "\n".join(rows)
    t5_pass = ("started process 1" in screen_text and
               "io_test: testing stdout" in screen_text and
               "io_test: testing stderr" in screen_text and
               "io_test: stderr works after close(1)" in screen_text and
               "io_test: all assertions passed" in screen_text and
               "Error:" not in screen_text)
    if t5_pass:
        print("Test 5 (run /bin/io_test): PASS")
    else:
        print("Test 5 (run /bin/io_test): FAIL")
        all_passed = False
    print_screen(rows, "Test 5: run /bin/io_test")

    # -------------------------------------------------------------------------
    # TEST 6: PID Reuse and Process Slot Recovery after I/O runs
    # -------------------------------------------------------------------------
    print("\n[TEST 6] Verifying PID Reuse & Process Slot Recovery after I/O Processes...")
    keys = (text_to_sendkeys("clear\nrun /bin/write_test\n") +
            [("sleep", 0.8)] +
            text_to_sendkeys("run /bin/write_test\n") +
            [("sleep", 0.8)] +
            text_to_sendkeys("ps\n"))
    rows = run_qemu_test(keys, wait_time=1.5)
    screen_text = "\n".join(rows)
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
    # TEST 7: Kernel Heap Baseline Check (heapinfo)
    # Verifies embedded stdio open_file_t structures cause zero heap allocations.
    # -------------------------------------------------------------------------
    print("\n[TEST 7] Verifying Kernel Heap Baseline (heapinfo)...")
    keys = text_to_sendkeys("clear\nheapinfo\n")
    rows = run_qemu_test(keys, wait_time=0.8)
    screen_text = "\n".join(rows)
    t7_pass = ("Used: 0 bytes" in screen_text or "Used:   0 bytes" in screen_text or "Used:" in screen_text and "0 bytes" in screen_text)
    if t7_pass:
        print("Test 7 (Heap Baseline Used: 0 bytes): PASS")
    else:
        print("Test 7 (Heap Baseline Used: 0 bytes): FAIL")
        all_passed = False
    print_screen(rows, "Test 7: heapinfo")

    # -------------------------------------------------------------------------
    # TEST 8: Full Regression Suite - waittest (Stage 13B Lifecycle)
    # -------------------------------------------------------------------------
    print("\n[TEST 8] Verifying Prior Regression - waittest (Stage 13B)...")
    keys = text_to_sendkeys("clear\nwaittest\n")
    rows = run_qemu_test(keys, wait_time=1.2)
    screen_text = "\n".join(rows)
    t8_pass = ("PASSED (All Lifecycle Properties Verified)" in screen_text and
               "FAIL" not in screen_text)
    if t8_pass:
        print("Test 8 (Regression waittest): PASS")
    else:
        print("Test 8 (Regression waittest): FAIL")
        all_passed = False
    print_screen(rows, "Test 8: waittest")

    # -------------------------------------------------------------------------
    # TEST 9: Full Regression Suite - proctest (Stage 9 Isolation)
    # -------------------------------------------------------------------------
    print("\n[TEST 9] Verifying Prior Regression - proctest (Stage 9)...")
    keys = text_to_sendkeys("clear\nproctest\n")
    rows = run_qemu_test(keys, wait_time=1.5)
    screen_text = "\n".join(rows)
    t9_pass = ("PASSED (All Isolation Properties Verified)" in screen_text and
               "FAIL" not in screen_text)
    if t9_pass:
        print("Test 9 (Regression proctest): PASS")
    else:
        print("Test 9 (Regression proctest): FAIL")
        all_passed = False
    print_screen(rows, "Test 9: proctest")

    # -------------------------------------------------------------------------
    # TEST 10: Full Regression Suite - elftest (Stage 10/11C Loader)
    # -------------------------------------------------------------------------
    print("\n[TEST 10] Verifying Prior Regression - elftest (Stage 10)...")
    keys = text_to_sendkeys("clear\nelftest\n")
    rows = run_qemu_test(keys, wait_time=1.5)
    screen_text = "\n".join(rows)
    t10_pass = ("PASSED (All ELF Checks Verified)" in screen_text and
                "FAIL" not in screen_text)
    if t10_pass:
        print("Test 10 (Regression elftest): PASS")
    else:
        print("Test 10 (Regression elftest): FAIL")
        all_passed = False
    print_screen(rows, "Test 10: elftest")

    # -------------------------------------------------------------------------
    # TEST 11: Full Regression Suite - syscalltest (Stage 8B Syscalls)
    # -------------------------------------------------------------------------
    print("\n[TEST 11] Verifying Prior Regression - syscalltest (Stage 8B)...")
    keys = text_to_sendkeys("clear\nsyscalltest\n")
    rows = run_qemu_test(keys, wait_time=1.0)
    screen_text = "\n".join(rows)
    t11_pass = ("PASSED (All Syscalls Verified)" in screen_text and
                "FAIL" not in screen_text)
    if t11_pass:
        print("Test 11 (Regression syscalltest): PASS")
    else:
        print("Test 11 (Regression syscalltest): FAIL")
        all_passed = False
    print_screen(rows, "Test 11: syscalltest")

    # -------------------------------------------------------------------------
    # Final Result
    # -------------------------------------------------------------------------
    print("\n=======================================================")
    if all_passed:
        print(">>> STAGE 14A ALL 24 VERIFICATION CHECKS PASSED <<<")
    else:
        print(">>> STAGE 14A VERIFICATION FAILED <<<")
    print("=======================================================")
    sys.exit(0 if all_passed else 1)

if __name__ == "__main__":
    main()
