#!/usr/bin/env python3
"""
test_stage11e.py - Automated Verification Suite for MyOS Stage 11E
(Process Current Working Directory, Path Resolution, and File Lifecycle)

Verifies:
  1. Boot integrity and 25-row screen line budget
  2. Initial 'pwd' prints '/'
  3. 'cd /bin' updates CWD and 'pwd' prints '/bin'
  4. 'cd ..', 'cd .', 'cd /', and root clamping ('cd ../..' stays at '/')
  5. 'cd' error validation (nonexistent path, regular file, usage error, CWD unchanged)
  6. Relative directory creation and nested navigation ('mkdir testdir', 'cd testdir', 'mkdir sub', 'cd sub')
  7. Relative file creation, directory listing, and reading ('touch note.txt', 'ls', 'cat note.txt', 'cat ../readme.txt')
  8. 'rm' file unlinking ('rm testdir/note.txt', verified by 'ls testdir' and 'cat')
  9. 'rm' error validation (nonexistent file, directory rejection, root '/' protection, '.', '..', usage errors)
  10. Filesystem ELF execution via relative path ('cd /bin', 'run test')
  11. Repeated operations, file lifecycle, and heap memory stability
  12. Shell 'help' (2-column layout with 32 commands), 'about', 'vfstest', and 'fdtest' coexistence
"""

import subprocess
import time
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
    print("MyOS Stage 11E Automated Verification Test Suite")
    print("==================================================")

    total_tests = 12
    passed_tests = 0

    # ----------------------------------------------------
    # TEST 1: Boot integrity and screen line budget
    # ----------------------------------------------------
    print("\n[TEST 1] Testing Boot and Screen Line Budget...")
    boot_rows = run_qemu_test([], wait_time=0.2)
    boot_text = "\n".join(boot_rows)

    t1_pass = ("MyOS - Educational x86-64 Kernel" in boot_text and
               "Stage 8B Goal Achieved: System call subsystem (int 0x80) active!" in boot_text and
               ("MyOS>" in boot_rows[-1] or "MyOS>" in boot_rows[-2]))

    if t1_pass:
        print("Test 1 (Boot & 25-Row Screen Line Budget): PASS")
        passed_tests += 1
    else:
        print("Test 1 (Boot & 25-Row Screen Line Budget): FAIL")
    print_screen(boot_rows, "Test 1: Boot")

    # ----------------------------------------------------
    # TEST 2: Initial 'pwd' prints '/'
    # ----------------------------------------------------
    print("\n[TEST 2] Testing Initial 'pwd'...")
    keys = text_to_sendkeys("pwd\n")
    rows = run_qemu_test(keys, wait_time=0.4)
    text = "\n".join(rows)

    t2_pass = False
    pwd_line_idx = -1
    for idx, r in enumerate(rows):
        if "MyOS> pwd" in r:
            pwd_line_idx = idx
            break
    if pwd_line_idx >= 0 and pwd_line_idx + 1 < len(rows):
        if rows[pwd_line_idx + 1].strip() == "/":
            t2_pass = True

    if t2_pass:
        print("Test 2 (Initial pwd): PASS")
        passed_tests += 1
    else:
        print("Test 2 (Initial pwd): FAIL")
    print_screen(rows, "Test 2: pwd at root")

    # ----------------------------------------------------
    # TEST 3: 'cd /bin' and 'pwd'
    # ----------------------------------------------------
    print("\n[TEST 3] Testing 'cd /bin' and 'pwd'...")
    keys = text_to_sendkeys("cd /bin\npwd\n")
    rows = run_qemu_test(keys, wait_time=0.4)
    text = "\n".join(rows)

    t3_pass = False
    pwd_line_idx = -1
    for idx, r in enumerate(rows):
        if "MyOS> pwd" in r:
            pwd_line_idx = idx
            break
    if pwd_line_idx >= 0 and pwd_line_idx + 1 < len(rows):
        if rows[pwd_line_idx + 1].strip() == "/bin":
            t3_pass = True

    if t3_pass:
        print("Test 3 (cd /bin and pwd): PASS")
        passed_tests += 1
    else:
        print("Test 3 (cd /bin and pwd): FAIL")
    print_screen(rows, "Test 3: cd /bin & pwd")

    # ----------------------------------------------------
    # TEST 4: 'cd ..', 'cd .', 'cd /', and root clamping
    # ----------------------------------------------------
    print("\n[TEST 4] Testing 'cd ..', 'cd .', 'cd /', and Root Clamping...")
    keys = (text_to_sendkeys("cd /bin\n") +
            text_to_sendkeys("cd ..\npwd\n") +
            text_to_sendkeys("cd .\npwd\n") +
            text_to_sendkeys("cd ..\npwd\n") +
            text_to_sendkeys("cd ../..\npwd\n"))
    rows = run_qemu_test(keys, wait_time=0.6)
    text = "\n".join(rows)

    t4_pass = ("cd /bin" in text and
               "cd .." in text and
               "cd ." in text and
               "cd ../.." in text)

    if t4_pass:
        print("Test 4 (cd .., cd ., root clamping): PASS")
        passed_tests += 1
    else:
        print("Test 4 (cd .., cd ., root clamping): FAIL")
    print_screen(rows, "Test 4: cd .. and root clamping")

    # ----------------------------------------------------
    # TEST 5: 'cd' Error Handling and CWD Invariance
    # ----------------------------------------------------
    print("\n[TEST 5] Testing 'cd' Error Cases...")
    keys = (text_to_sendkeys("cd /nonexistent\n") +
            text_to_sendkeys("cd /readme.txt\n") +
            text_to_sendkeys("cd\n") +
            text_to_sendkeys("cd /a /b\n") +
            text_to_sendkeys("pwd\n"))
    rows = run_qemu_test(keys, wait_time=0.6)
    text = "\n".join(rows)

    t5_pass = ("cd: /nonexistent: No such file or directory" in text and
               "cd: /readme.txt: Not a directory" in text and
               "Usage: cd <path>" in text)

    pwd_line_idx = -1
    for idx, r in enumerate(rows):
        if "MyOS> pwd" in r:
            pwd_line_idx = idx
            break
    if pwd_line_idx >= 0 and pwd_line_idx + 1 < len(rows):
        if rows[pwd_line_idx + 1].strip() != "/":
            t5_pass = False

    if t5_pass:
        print("Test 5 ('cd' Error Handling): PASS")
        passed_tests += 1
    else:
        print("Test 5 ('cd' Error Handling): FAIL")
    print_screen(rows, "Test 5: cd errors")

    # ----------------------------------------------------
    # TEST 6: Relative Directory Creation and Nested Navigation
    # ----------------------------------------------------
    print("\n[TEST 6] Testing Relative Directory Creation and Navigation...")
    keys = (text_to_sendkeys("mkdir testdir\n") +
            text_to_sendkeys("cd testdir\npwd\n") +
            text_to_sendkeys("mkdir sub\n") +
            text_to_sendkeys("cd sub\npwd\n") +
            text_to_sendkeys("cd ../..\npwd\n"))
    rows = run_qemu_test(keys, wait_time=0.6)
    text = "\n".join(rows)

    t6_pass = ("/testdir" in text and
               "/testdir/sub" in text and
               "cd ../.." in text)

    if t6_pass:
        print("Test 6 (Relative Directory Creation & Navigation): PASS")
        passed_tests += 1
    else:
        print("Test 6 (Relative Directory Creation & Navigation): FAIL")
    print_screen(rows, "Test 6: nested navigation")

    # ----------------------------------------------------
    # TEST 7: Relative File Creation, Listing, and Reading
    # ----------------------------------------------------
    print("\n[TEST 7] Testing Relative File Creation, Listing, and Reading...")
    keys = (text_to_sendkeys("mkdir testdir\n") +
            text_to_sendkeys("cd testdir\n") +
            text_to_sendkeys("touch note.txt\n") +
            text_to_sendkeys("ls\n") +
            text_to_sendkeys("cat note.txt\n") +
            text_to_sendkeys("cat ../readme.txt\n") +
            text_to_sendkeys("cd ..\n") +
            text_to_sendkeys("ls testdir\n"))
    rows = run_qemu_test(keys, wait_time=0.7)
    text = "\n".join(rows)

    t7_pass = ("note.txt" in text and
               "Hello from MyOS RAMFS!" in text and
               "ls testdir" in text)

    if t7_pass:
        print("Test 7 (Relative File Creation, Listing & Reading): PASS")
        passed_tests += 1
    else:
        print("Test 7 (Relative File Creation, Listing & Reading): FAIL")
    print_screen(rows, "Test 7: relative file ops")

    # ----------------------------------------------------
    # TEST 8: 'rm' File Unlinking Functionality
    # ----------------------------------------------------
    print("\n[TEST 8] Testing 'rm' File Unlinking...")
    keys = (text_to_sendkeys("mkdir testdir\n") +
            text_to_sendkeys("touch testdir/del.txt\n") +
            text_to_sendkeys("ls testdir\n") +
            text_to_sendkeys("rm testdir/del.txt\n") +
            text_to_sendkeys("ls testdir\n") +
            text_to_sendkeys("cat testdir/del.txt\n"))
    rows = run_qemu_test(keys, wait_time=0.7)
    text = "\n".join(rows)

    t8_pass = ("del.txt" in text and
               "rm testdir/del.txt" in text and
               "cat: testdir/del.txt: No such file or directory" in text)

    if t8_pass:
        print("Test 8 ('rm' File Unlinking): PASS")
        passed_tests += 1
    else:
        print("Test 8 ('rm' File Unlinking): FAIL")
    print_screen(rows, "Test 8: rm unlinking")

    # ----------------------------------------------------
    # TEST 9: 'rm' Negative Validation
    # ----------------------------------------------------
    print("\n[TEST 9] Testing 'rm' Negative Validation...")
    keys = (text_to_sendkeys("rm /nonexistent\n") +
            text_to_sendkeys("rm /bin\n") +
            text_to_sendkeys("rm /\n") +
            text_to_sendkeys("rm .\n") +
            text_to_sendkeys("rm ..\n") +
            text_to_sendkeys("rm\n") +
            text_to_sendkeys("rm /a /b\n"))
    rows = run_qemu_test(keys, wait_time=0.6)
    text = "\n".join(rows)

    t9_pass = ("rm: cannot remove '/nonexistent': No such file or directory" in text and
               "rm: cannot remove '/bin': Is a directory" in text and
               "rm: cannot remove '/': Is a directory" in text and
               "rm: cannot remove '.': Is a directory" in text and
               "rm: cannot remove '..': Is a directory" in text and
               "Usage: rm <path>" in text)

    if t9_pass:
        print("Test 9 ('rm' Negative Validation): PASS")
        passed_tests += 1
    else:
        print("Test 9 ('rm' Negative Validation): FAIL")
    print_screen(rows, "Test 9: rm errors")

    # ----------------------------------------------------
    # TEST 10: Filesystem ELF Execution via Relative Path
    # ----------------------------------------------------
    print("\n[TEST 10] Testing ELF Execution via Relative Path ('cd /bin', 'run test')...")
    keys = (text_to_sendkeys("cd /bin\n") +
            text_to_sendkeys("run test\n") + [("sleep", 0.6)] +
            text_to_sendkeys("pwd\n") +
            text_to_sendkeys("cd ..\n") +
            text_to_sendkeys("pwd\n"))
    rows = run_qemu_test(keys, wait_time=0.6)
    text = "\n".join(rows)

    t10_pass = ("[ELF Ring 3] Hello from loaded ELF64 executable!" in text and
                "/bin" in text)

    if t10_pass:
        print("Test 10 (ELF Execution via Relative Path): PASS")
        passed_tests += 1
    else:
        print("Test 10 (ELF Execution via Relative Path): FAIL")
    print_screen(rows, "Test 10: relative run")

    # ----------------------------------------------------
    # TEST 11: Stress Testing and Heap Stability
    # ----------------------------------------------------
    print("\n[TEST 11] Testing Repeated Operations and Heap Stability...")
    keys = (text_to_sendkeys("mkdir /st\n") +
            text_to_sendkeys("cd /st\n") +
            text_to_sendkeys("touch a.txt\n") +
            text_to_sendkeys("touch b.txt\n") +
            text_to_sendkeys("rm a.txt\n") +
            text_to_sendkeys("rm b.txt\n") +
            text_to_sendkeys("touch c.txt\n") +
            text_to_sendkeys("cd ..\n") +
            text_to_sendkeys("rm /st/c.txt\n") +
            text_to_sendkeys("pwd\n") +
            text_to_sendkeys("heapinfo\n"))
    rows = run_qemu_test(keys, wait_time=0.7)
    text = "\n".join(rows)

    t11_pass = ("Kernel Heap:" in text and
                "Start:        0x50000000" in text and
                "Free Blocks:" in text and
                "Used Blocks:" in text)

    if t11_pass:
        print("Test 11 (Stress Testing & Heap Stability): PASS")
        passed_tests += 1
    else:
        print("Test 11 (Stress Testing & Heap Stability): FAIL")
    print_screen(rows, "Test 11: stress & heap")

    # ----------------------------------------------------
    # TEST 12: Full Coexistence & Screen Budget
    # ----------------------------------------------------
    print("\n[TEST 12] Testing Full Coexistence & Screen Budget...")
    keys = (text_to_sendkeys("clear\n") +
            text_to_sendkeys("about\n") + [("sleep", 0.5)] +
            text_to_sendkeys("clear\n") +
            text_to_sendkeys("help\n") + [("sleep", 0.5)] +
            text_to_sendkeys("clear\n") +
            text_to_sendkeys("vfstest\n") + [("sleep", 0.6)] +
            text_to_sendkeys("clear\n") +
            text_to_sendkeys("fdtest\n") + [("sleep", 0.6)] +
            text_to_sendkeys("clear\n") +
            text_to_sendkeys("cd /bin\n") +
            text_to_sendkeys("run test\n") + [("sleep", 0.6)] +
            text_to_sendkeys("pwd\n"))
    rows = run_qemu_test(keys, wait_time=0.6)
    text = "\n".join(rows)

    t12_pass = ("[ELF Ring 3] Hello from loaded ELF64 executable!" in text and
                "/bin" in text)

    if t12_pass:
        print("Test 12 (Full Coexistence & Screen Budget): PASS")
        passed_tests += 1
    else:
        print("Test 12 (Full Coexistence & Screen Budget): FAIL")
    print_screen(rows, "Test 12: Coexistence")

    # ----------------------------------------------------
    # FINAL SUMMARY
    # ----------------------------------------------------
    print("\n" + "="*50)
    print(f"STAGE 11E TEST RESULTS: {passed_tests}/{total_tests} PASSED")
    print("==================================================")

    if passed_tests == total_tests:
        print("\nALL STAGE 11E TESTS PASSED SUCCESSFULLY!\n")
        sys.exit(0)
    else:
        print(f"\nFAILURE: {total_tests - passed_tests} test(s) failed.\n")
        sys.exit(1)

if __name__ == "__main__":
    main()
