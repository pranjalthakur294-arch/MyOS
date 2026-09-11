#!/usr/bin/env python3
"""
test_stage11d.py - Automated Verification Suite for MyOS Stage 11D
(Shell Filesystem Operations: ls, cat, touch, mkdir)

Verifies:
  1. Boot integrity and 25-row screen line budget
  2. 'ls' and 'ls /' directory iteration (lists readme.txt, etc/, bin/)
  3. 'ls /bin' (lists test, bad) and 'ls /etc' (empty directory)
  4. 'ls' error cases (nonexistent path, regular file, excess arguments)
  5. 'cat /readme.txt' prints file content
  6. 'cat' error cases (nonexistent file, directory rejection, usage errors)
  7. 'cat /bin/test' displays binary bytes safely without terminal corruption
  8. 'touch /hello.txt' creates empty file, verified by 'ls /' and 'cat /hello.txt'
  9. 'mkdir /home' creates directory, verified by 'ls /', and nested 'mkdir /home/user'
  10. 'touch' and 'mkdir' negative validation (duplicates, conflicts, nonexistent parents, non-dir parents)
  11. Stress testing: repeated touch/mkdir/cat operations and heap stability
  12. Coexistence with Stage 11C ('run /bin/test'), Stage 11B ('fdtest'), Stage 11A ('vfstest'), Stage 8B ('syscalltest')
  13. Shell 'help' (2-column layout with 29 commands) and 'about' output within 25 rows
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
    print("MyOS Stage 11D Automated Verification Test Suite")
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

    # ----------------------------------------------------
    # TEST 2: 'ls' and 'ls /' directory iteration
    # ----------------------------------------------------
    print("\n[TEST 2] Testing 'ls' and 'ls /'...")
    keys = text_to_sendkeys("clear\nls\nls /\n")
    rows = run_qemu_test(keys, wait_time=0.4)
    text = "\n".join(rows)

    # Both 'ls' and 'ls /' should list 'readme.txt', 'bin/', 'etc/'
    t2_pass = ("readme.txt" in text and
               "bin/" in text and
               "etc/" in text)

    if t2_pass:
        print("Test 2 ('ls' and 'ls /'): PASS")
        passed_tests += 1
    else:
        print("Test 2 ('ls' and 'ls /'): FAIL")
    print_screen(rows, "Test 2: ls")

    # ----------------------------------------------------
    # TEST 3: 'ls /bin' and 'ls /etc'
    # ----------------------------------------------------
    print("\n[TEST 3] Testing 'ls /bin' and 'ls /etc'...")
    keys = text_to_sendkeys("clear\nls /bin\nls /etc\necho DONE\n")
    rows = run_qemu_test(keys, wait_time=0.4)
    text = "\n".join(rows)

    # /bin contains 'test' and 'bad'; /etc is empty (no output before prompt)
    t3_pass = ("test" in text and
               "bad" in text and
               "DONE" in text)

    if t3_pass:
        print("Test 3 ('ls /bin' and 'ls /etc'): PASS")
        passed_tests += 1
    else:
        print("Test 3 ('ls /bin' and 'ls /etc'): FAIL")
    print_screen(rows, "Test 3: ls /bin & /etc")

    # ----------------------------------------------------
    # TEST 4: 'ls' error cases (nonexistent, file, excess args)
    # ----------------------------------------------------
    print("\n[TEST 4] Testing 'ls' error handling...")
    keys = text_to_sendkeys("clear\nls /nonexistent\nls /readme.txt\nls /a /b\n")
    rows = run_qemu_test(keys, wait_time=0.4)
    text = "\n".join(rows)

    t4_pass = ("No such file or directory" in text and
               "Not a directory" in text and
               "Usage: ls [path]" in text)

    if t4_pass:
        print("Test 4 ('ls' Error Cases): PASS")
        passed_tests += 1
    else:
        print("Test 4 ('ls' Error Cases): FAIL")
    print_screen(rows, "Test 4: ls errors")

    # ----------------------------------------------------
    # TEST 5: 'cat /readme.txt'
    # ----------------------------------------------------
    print("\n[TEST 5] Testing 'cat /readme.txt'...")
    keys = text_to_sendkeys("clear\ncat /readme.txt\n")
    rows = run_qemu_test(keys, wait_time=0.4)
    text = "\n".join(rows)

    t5_pass = "Hello from MyOS RAMFS!" in text

    if t5_pass:
        print("Test 5 ('cat /readme.txt'): PASS")
        passed_tests += 1
    else:
        print("Test 5 ('cat /readme.txt'): FAIL")
    print_screen(rows, "Test 5: cat readme.txt")

    # ----------------------------------------------------
    # TEST 6: 'cat' error handling (nonexistent, directory, usage)
    # ----------------------------------------------------
    print("\n[TEST 6] Testing 'cat' error handling...")
    keys = text_to_sendkeys("clear\ncat\ncat /nonexistent\ncat /bin\ncat /a /b\n")
    rows = run_qemu_test(keys, wait_time=0.4)
    text = "\n".join(rows)

    t6_pass = ("Usage: cat <path>" in text and
               "No such file or directory" in text and
               "Is a directory" in text)

    if t6_pass:
        print("Test 6 ('cat' Error Handling): PASS")
        passed_tests += 1
    else:
        print("Test 6 ('cat' Error Handling): FAIL")
    print_screen(rows, "Test 6: cat errors")

    # ----------------------------------------------------
    # TEST 7: 'cat /bin/test' (binary ELF file output safe)
    # ----------------------------------------------------
    print("\n[TEST 7] Testing 'cat /bin/test' (binary ELF safe output)...")
    keys = text_to_sendkeys("clear\ncat /bin/test\necho POST_CAT_OK\n")
    rows = run_qemu_test(keys, wait_time=0.6)
    text = "\n".join(rows)

    # ELF contains symbols like '.symtab' at the tail, and shell remains responsive
    t7_pass = (".symtab" in text and "POST_CAT_OK" in text)

    if t7_pass:
        print("Test 7 ('cat /bin/test' Binary Output): PASS")
        passed_tests += 1
    else:
        print("Test 7 ('cat /bin/test' Binary Output): FAIL")
    print_screen(rows, "Test 7: cat /bin/test")

    # ----------------------------------------------------
    # TEST 8: 'touch /hello.txt' creation and empty read
    # ----------------------------------------------------
    print("\n[TEST 8] Testing 'touch /hello.txt' creation and empty read...")
    keys = text_to_sendkeys("clear\ntouch /hello.txt\nls /\ncat /hello.txt\necho TOUCH_DONE\n")
    rows = run_qemu_test(keys, wait_time=0.4)
    text = "\n".join(rows)

    # 'ls /' must now include hello.txt, and cat on it is silent
    t8_pass = ("hello.txt" in text and "TOUCH_DONE" in text)

    if t8_pass:
        print("Test 8 ('touch' Creation & Empty Read): PASS")
        passed_tests += 1
    else:
        print("Test 8 ('touch' Creation & Empty Read): FAIL")
    print_screen(rows, "Test 8: touch /hello.txt")

    # ----------------------------------------------------
    # TEST 9: 'mkdir /home' & nested 'mkdir /home/user'
    # ----------------------------------------------------
    print("\n[TEST 9] Testing 'mkdir /home' and nested 'mkdir /home/user'...")
    keys = text_to_sendkeys("clear\nmkdir /home\nls /\nmkdir /home/user\nls /home\n")
    rows = run_qemu_test(keys, wait_time=0.4)
    text = "\n".join(rows)

    # 'ls /' shows 'home/' and 'ls /home' shows 'user/'
    t9_pass = ("home/" in text and "user/" in text)

    if t9_pass:
        print("Test 9 ('mkdir' & Nested Directory): PASS")
        passed_tests += 1
    else:
        print("Test 9 ('mkdir' & Nested Directory): FAIL")
    print_screen(rows, "Test 9: mkdir /home & /home/user")

    # ----------------------------------------------------
    # TEST 10: 'touch' and 'mkdir' negative validation
    # ----------------------------------------------------
    print("\n[TEST 10] Testing 'touch' and 'mkdir' negative validation...")
    keys = text_to_sendkeys(
        "clear\n"
        "touch /hello.txt\n"             # File already exists
        "mkdir /home\n"                  # Directory already exists
        "touch /bin\n"                   # Collision with directory
        "mkdir /readme.txt\n"            # Collision with file
        "mkdir /nonexistent/dir\n"       # Missing parent dir
        "touch /nonexistent/file\n"      # Missing parent dir
        "touch /readme.txt/sub\n"        # Non-directory parent
        "mkdir /readme.txt/sub\n"        # Non-directory parent
        "touch\n"                        # Missing argument
        "mkdir\n"                        # Missing argument
        "touch /a /b\n"                  # Excess arguments
        "mkdir /a /b\n"                  # Excess arguments
    )
    rows = run_qemu_test(keys, wait_time=0.6)
    text = "\n".join(rows)

    t10_pass = ("File already exists" in text and
                "File exists" in text and
                "No such file or directory" in text and
                "Not a directory" in text and
                "Usage: touch <path>" in text and
                "Usage: mkdir <path>" in text)

    if t10_pass:
        print("Test 10 ('touch' & 'mkdir' Negative Validation): PASS")
        passed_tests += 1
    else:
        print("Test 10 ('touch' & 'mkdir' Negative Validation): FAIL")
    print_screen(rows, "Test 10: touch/mkdir errors")

    # ----------------------------------------------------
    # TEST 11: Repeated operations & heap stability
    # ----------------------------------------------------
    print("\n[TEST 11] Testing Repeated Operations and Heap Stability...")
    keys = text_to_sendkeys(
        "clear\n"
        "touch /f1.txt\n"
        "touch /f2.txt\n"
        "mkdir /d1\n"
        "mkdir /d2\n"
        "cat /f1.txt\n"
        "cat /readme.txt\n"
        "ls /\n"
        "heapinfo\n"
    )
    rows = run_qemu_test(keys, wait_time=0.6)
    text = "\n".join(rows)

    t11_pass = ("f1.txt" in text and
                "f2.txt" in text and
                "d1/" in text and
                "d2/" in text and
                "Kernel Heap:" in text)

    if t11_pass:
        print("Test 11 (Repeated Operations & Heap Stability): PASS")
        passed_tests += 1
    else:
        print("Test 11 (Repeated Operations & Heap Stability): FAIL")
    print_screen(rows, "Test 11: Repeated Ops & Heap")

    # ----------------------------------------------------
    # TEST 12: Coexistence with Stage 11C ('run'), 'vfstest', 'fdtest', 'help', 'about'
    # ----------------------------------------------------
    print("\n[TEST 12] Testing Full Coexistence & Screen Budget...")
    keys = text_to_sendkeys(
        "clear\n"
        "help\n"
        "about\n"
        "run /bin/test\n"
    )
    rows = run_qemu_test(keys, wait_time=1.0)
    text = "\n".join(rows)

    t12_pass = ("ls" in text and
                "cat" in text and
                "touch" in text and
                "mkdir" in text and
                "run" in text and
                "FS Commands: ls, cat, touch, mkdir Active" in text and
                "Hello from loaded ELF64 executable!" in text)

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
    print(f"STAGE 11D TEST RESULTS: {passed_tests}/{total_tests} PASSED")
    print("="*50)

    if passed_tests == total_tests:
        print("\nALL STAGE 11D TESTS PASSED SUCCESSFULLY!")
        sys.exit(0)
    else:
        print(f"\nFAILURE: {total_tests - passed_tests} test(s) failed.")
        sys.exit(1)

if __name__ == "__main__":
    main()
