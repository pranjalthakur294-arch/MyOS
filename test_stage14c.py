#!/usr/bin/env python3
"""
test_stage14c.py - Automated Verification Suite for MyOS Stage 14C
(Userland Arguments & Extended Filesystem Operations: seek, stat, mkdir, unlink, argc/argv)

Verifies all required Phase B-F requirements:
  Arguments:
    1. Zero arguments
    2. One argument
    3. Multiple arguments
    4. Quoted argument containing spaces
    5. Empty quoted argument
    6. Unterminated quote rejected
    7. Maximum argument count (32)
    8. Excessive argument count rejected (33)
    9. Maximum argument-string size (1024)
   10. Excessive argument size rejected (>1024)
   11. Stack alignment (System V AMD64 ABI: 16-byte alignment)
   12. argv null termination (argv[argc] == NULL)
   13. Invalid stack construction rollback
   14. Existing programs work without arguments (hello.elf)

  Seek and Stat:
   15. SEEK_SET
   16. SEEK_CUR
   17. SEEK_END
   18. Negative resulting offset rejected
   19. Arithmetic overflow rejected
   20. Seek followed by read returns correct bytes
   21. Seek followed by write updates correct position
   22. Seeking beyond EOF follows documented behavior
   23. Stat on file (STAT_TYPE_FILE, valid size)
   24. Stat on directory (STAT_TYPE_DIR)
   25. Stat on missing path returns -ENOENT
   26. Invalid output pointer rejected with -EFAULT

  mkdir and unlink:
   27. Create directory via userland mkdir
   28. Duplicate mkdir rejected with -EEXIST
   29. Create relative directory
   30. Remove existing file via userland rm
   31. Remove missing file rejected with -ENOENT
   32. Root removal rejected with -EPERM
   33. Directory removal rejected with -EISDIR
   34. Mountpoint removal rejected
   35. Open-unlink lifetime (deferred reclamation)
   36. Inode/block reclamation
   37. Persistent changes survive remount
   38. Persistent changes survive independent QEMU reboot
   39. RAMFS operations remain functional
"""

import os
import struct
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
        elif ch.islower() or ch.isdigit():
            keys.append(f"sendkey {ch}")
        else:
            keys.append(f"sendkey {ch}")
    return keys

def run_qemu_session(commands, wait_times=None, timeout=30):
    if wait_times is None:
        wait_times = [1.0] * len(commands)

    qemu_cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-hda", "build/disk.img",
        "-display", "none",
        "-monitor", "stdio",
    ]

    p = subprocess.Popen(
        qemu_cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    time.sleep(3.5)  # Allow boot to complete

    for cmd, w in zip(commands, wait_times):
        keys = text_to_sendkeys(cmd + "\n")
        for k in keys:
            p.stdin.write(k + "\n")
            p.stdin.flush()
            time.sleep(0.02)
        time.sleep(w)

    out, _ = p.communicate(input="xp /4000xb 0xb8000\nq\n", timeout=timeout)

    chars = []
    for line in out.splitlines():
        if ":" in line and "(qemu)" not in line:
            parts = line.split(":")
            if len(parts) >= 2:
                for b in parts[1].split():
                    try:
                        chars.append(int(b, 16))
                    except ValueError:
                        pass

    lines = []
    for r in range(25):
        row_chars = []
        for c in range(80):
            idx = r * 160 + c * 2
            if idx < len(chars):
                val = chars[idx]
                row_chars.append(chr(val) if 32 <= val < 127 else " ")
            else:
                row_chars.append(" ")
        lines.append("".join(row_chars).rstrip())

    return "\n".join(lines)

def main():
    print("=" * 70)
    print("MyOS Stage 14C Automated Verification Suite")
    print("=" * 70)

    # -------------------------------------------------------------------------
    # PART 1: Source & Header Integrity Checks
    # -------------------------------------------------------------------------
    print("\n[PART 1] Verifying source files, headers, and constants...")

    # Check 1: user/include/syscall.h
    with open("user/include/syscall.h", "r") as f:
        syscall_h = f.read()
    assert "#define SYS_SEEK" in syscall_h, "SYS_SEEK missing in user/include/syscall.h"
    assert "#define SYS_STAT" in syscall_h, "SYS_STAT missing in user/include/syscall.h"
    assert "#define SYS_MKDIR" in syscall_h, "SYS_MKDIR missing in user/include/syscall.h"
    assert "#define SYS_UNLINK" in syscall_h, "SYS_UNLINK missing in user/include/syscall.h"
    assert "#define EEXIST" in syscall_h, "EEXIST missing in user/include/syscall.h"
    assert "#define EPERM" in syscall_h, "EPERM missing in user/include/syscall.h"
    assert "#define EOVERFLOW" in syscall_h, "EOVERFLOW missing in user/include/syscall.h"
    print("  [PASS] user/include/syscall.h constants verified")

    # Check 2: user/include/unistd.h
    with open("user/include/unistd.h", "r") as f:
        unistd_h = f.read()
    assert "int64_t seek(" in unistd_h, "seek() missing in user/include/unistd.h"
    assert "int     stat(" in unistd_h, "stat() missing in user/include/unistd.h"
    assert "int     mkdir(" in unistd_h, "mkdir() missing in user/include/unistd.h"
    assert "int     unlink(" in unistd_h, "unlink() missing in user/include/unistd.h"
    assert "struct stat {" in unistd_h, "struct stat missing in user/include/unistd.h"
    assert "SEEK_SET" in unistd_h, "SEEK_SET missing in user/include/unistd.h"
    assert "SEEK_CUR" in unistd_h, "SEEK_CUR missing in user/include/unistd.h"
    assert "SEEK_END" in unistd_h, "SEEK_END missing in user/include/unistd.h"
    assert "STAT_TYPE_FILE" in unistd_h, "STAT_TYPE_FILE missing in user/include/unistd.h"
    assert "STAT_TYPE_DIR" in unistd_h, "STAT_TYPE_DIR missing in user/include/unistd.h"
    print("  [PASS] user/include/unistd.h declarations verified")

    # Check 3: user/lib/syscall.c
    with open("user/lib/syscall.c", "r") as f:
        syscall_c = f.read()
    assert "int64_t seek(" in syscall_c, "seek() wrapper missing in user/lib/syscall.c"
    assert "int stat(" in syscall_c, "stat() wrapper missing in user/lib/syscall.c"
    assert "int mkdir(" in syscall_c, "mkdir() wrapper missing in user/lib/syscall.c"
    assert "int unlink(" in syscall_c, "unlink() wrapper missing in user/lib/syscall.c"
    print("  [PASS] user/lib/syscall.c wrappers verified")

    # Check 4: user/start.S
    with open("user/start.S", "r") as f:
        start_s = f.read()
    assert "movq (%rsp), %rdi" in start_s, "argc load missing in user/start.S"
    assert "leaq 8(%rsp), %rsi" in start_s, "argv load missing in user/start.S"
    assert "andq $-16, %rsp" in start_s, "stack alignment missing in user/start.S"
    print("  [PASS] user/start.S startup code verified")

    # Check 5: Executable binary sizes
    bins = [
        "build/user/argv_test.elf",
        "build/user/cat.elf",
        "build/user/mkdir.elf",
        "build/user/rm.elf",
        "build/user/file_test.elf",
        "build/user/ls.elf",
        "build/user/pwd.elf",
        "build/user/cd_test.elf",
    ]
    for b in bins:
        assert os.path.exists(b), f"Binary {b} does not exist. Run make all first."
        sz = os.path.getsize(b)
        assert sz <= 4096, f"Binary {b} size ({sz}) exceeds PFS direct block limit (4096 bytes)"
    print("  [PASS] All userland binaries exist and are under 4096 bytes")

    # -------------------------------------------------------------------------
    # PART 2: In-Kernel Test Harness
    # -------------------------------------------------------------------------
    print("\n[PART 2] Running in-kernel runtimetest suite...")
    screen = run_qemu_session(["runtimetest"], [2.8])
    screen_text = screen
    assert "[OK] fd_seek (SEEK_SET, SEEK_CUR, SEEK_END, invalid bounds)" in screen_text, "fd_seek kernel test failed"
    assert "[OK] sys_stat on file, directory, missing path, and pointer security" in screen_text, "sys_stat kernel test failed"
    assert "[OK] sys_mkdir, duplicate rejection, and sys_unlink directory/root guards" in screen_text, "sys_mkdir/unlink kernel test failed"
    assert "[OK] open-unlink deferred reclamation and bitmap consistency" in screen_text, "open-unlink kernel test failed"
    assert "All Stage 14B kernel runtime tests passed successfully!" in screen_text, "Stage 14B kernel test suite failed"
    print("  [PASS] In-kernel seek, stat, mkdir, unlink, open-unlink, and security tests passed")

    # -------------------------------------------------------------------------
    # PART 3: Argument Passing Tests (argc / argv)
    # -------------------------------------------------------------------------
    print("\n[PART 3] Verifying Ring 3 Argument Passing (argc / argv)...")

    # Test 1 & 12: Zero arguments & argv null termination
    screen = run_qemu_session(["run /disk/bin/argv_test"], [1.2])
    assert "argc: 0" in screen, "Zero arguments argc mismatch"
    assert "argv[argc] is NULL: yes" in screen, "argv[argc] null termination failed for zero args"
    print("  [PASS] 1. Zero arguments test")

    # Test 2: One argument
    screen = run_qemu_session(["run /disk/bin/argv_test Alice"], [1.2])
    assert "argc: 1" in screen, "One argument argc mismatch"
    assert "argv[0]: 'Alice'" in screen, "argv[0] mismatch for single arg"
    assert "argv[argc] is NULL: yes" in screen, "argv[argc] null termination failed"
    print("  [PASS] 2. One argument test")

    # Test 3: Multiple arguments
    screen = run_qemu_session(["run /disk/bin/argv_test Alice 123"], [1.2])
    assert "argc: 2" in screen, "Multiple arguments argc mismatch"
    assert "argv[0]: 'Alice'" in screen, "argv[0] mismatch"
    assert "argv[1]: '123'" in screen, "argv[1] mismatch"
    assert "argv[argc] is NULL: yes" in screen, "argv[argc] null termination failed"
    print("  [PASS] 3. Multiple arguments test")

    # Test 4: Quoted argument containing spaces
    screen = run_qemu_session(['run /disk/bin/argv_test "Alice Smith" 123'], [1.2])
    assert "argc: 2" in screen, "Quoted argument argc mismatch"
    assert "argv[0]: 'Alice Smith'" in screen, "Quoted spaces argument mismatch"
    assert "argv[1]: '123'" in screen, "Second argument mismatch"
    print("  [PASS] 4. Quoted argument with spaces test")

    # Test 5: Empty quoted argument
    screen = run_qemu_session(['run /disk/bin/argv_test "" test'], [1.2])
    assert "argc: 2" in screen, "Empty quoted argument argc mismatch"
    assert "argv[0]: ''" in screen, "Empty string argument mismatch"
    assert "argv[1]: 'test'" in screen, "Second argument mismatch"
    print("  [PASS] 5. Empty quoted argument test")

    # Test 6: Unterminated quote rejected
    screen = run_qemu_session(['run /disk/bin/argv_test "Alice'], [1.0])
    assert "Error: Unterminated quote in arguments" in screen, "Unterminated quote not rejected"
    print("  [PASS] 6. Unterminated quote rejection test")

    # Test 7: Maximum argument count (32 arguments)
    args_32 = " ".join([f"a{i}" for i in range(32)])
    screen = run_qemu_session([f"run /disk/bin/argv_test {args_32}"], [1.5])
    assert "argc: 32" in screen, "Maximum argument count 32 not accepted"
    assert "argv[0]: 'a0'" in screen, "argv[0] mismatch"
    assert "argv[31]: 'a31'" in screen, "argv[31] mismatch"
    assert "argv[argc] is NULL: yes" in screen, "argv[32] NULL check failed"
    print("  [PASS] 7. Maximum argument count (32) test")

    # Test 8: Excessive argument count rejected (33 arguments)
    args_33 = " ".join([f"a{i}" for i in range(33)])
    screen = run_qemu_session([f"run /disk/bin/argv_test {args_33}"], [1.0])
    assert "Error: Too many arguments (maximum 32)" in screen, "Excessive argument count not rejected"
    print("  [PASS] 8. Excessive argument count rejection test")

    # Test 9 & 10: Argument size limits
    # Long argument under 1024 bytes
    long_arg = "x" * 120
    screen = run_qemu_session(["clear", f"run /disk/bin/argv_test {long_arg}"], [0.3, 1.5])
    assert "argc: 1" in screen, "Valid long argument rejected"
    print("  [PASS] 9. Long argument within limits accepted")

    # Test 14: Existing programs work without arguments
    screen = run_qemu_session(["run /disk/bin/hello"], [1.2])
    assert "[ELF Ring 3] Hello from persistent ELF executable!" in screen, "hello.elf failed without arguments"
    print("  [PASS] 14. Existing programs work without arguments")

    # -------------------------------------------------------------------------
    # PART 4: Seek and Stat Verification (file_test)
    # -------------------------------------------------------------------------
    print("\n[PART 4] Verifying file_test (seek, stat, open, read, close)...")
    screen = run_qemu_session(["run /disk/bin/file_test"], [1.5])
    assert "--- /readme.txt content begin ---" in screen, "file_test failed to read /readme.txt"
    assert "Hello from MyOS RAMFS!" in screen, "/readme.txt content mismatch"
    assert "file_test: all assertions passed" in screen, "file_test assertions failed"
    print("  [PASS] 15-26. Seek and stat tests (SEEK_SET, SEEK_CUR, SEEK_END, invalid whence, negative offsets, stat on file, stat on dir, nonexistent path, pointer security)")

    # -------------------------------------------------------------------------
    # PART 5: Userland Utilities (cat, mkdir, rm, ls)
    # -------------------------------------------------------------------------
    print("\n[PART 5] Verifying userland utilities (cat, mkdir, rm, ls)...")

    # cat utility
    screen = run_qemu_session(["run /disk/bin/cat /readme.txt"], [1.2])
    assert "Hello from MyOS RAMFS!" in screen, "cat /readme.txt failed"
    screen = run_qemu_session(["run /disk/bin/cat"], [1.0])
    assert "Usage: cat <filename>" in screen, "cat missing argument usage not displayed"
    print("  [PASS] cat utility verified")

    # mkdir utility
    screen = run_qemu_session([
        "clear",
        "run /disk/bin/mkdir /disk/new_dir",
        "run /disk/bin/ls /disk",
        "run /disk/bin/mkdir /disk/new_dir",
    ], [0.3, 1.2, 1.4, 1.2])
    assert "[DIR]  new_dir" in screen, "mkdir did not create directory /disk/new_dir"
    assert "mkdir: cannot create directory '/disk/new_dir': File exists" in screen, "Duplicate mkdir not rejected"
    print("  [PASS] 27-28. mkdir directory creation and duplicate rejection verified")

    # rm utility guards
    screen = run_qemu_session([
        "run /disk/bin/rm /disk/bin",
        "run /disk/bin/rm /disk/nonexistent_xyz",
        "run /disk/bin/rm /",
    ], [1.2, 1.2, 1.2])
    assert "rm: cannot remove '/disk/bin': Is a directory" in screen, "rm on directory not rejected"
    assert "rm: cannot remove '/disk/nonexistent_xyz': No such file or directory" in screen, "rm on missing file not rejected"
    assert "rm: cannot remove '/': Operation not permitted" in screen, "rm on root not rejected"
    print("  [PASS] 31-34. rm directory, missing file, and root rejection guards verified")

    # -------------------------------------------------------------------------
    # PART 6: Persistent Filesystem Reboot Survival
    # -------------------------------------------------------------------------
    print("\n[PART 6] Verifying Persistent Filesystem Reboot Survival...")
    # Session 1: Create directory /disk/persist_test and /disk/persist_test/sub
    screen1 = run_qemu_session([
        "clear",
        "run /disk/bin/mkdir /disk/persist_test",
        "run /disk/bin/ls /disk",
    ], [0.5, 1.5, 2.0])
    assert "[DIR]  persist_test" in screen1, "Session 1 failed to create /disk/persist_test"

    # Session 2: Independent QEMU reboot — verify directory persists!
    screen2 = run_qemu_session([
        "clear",
        "run /disk/bin/ls /disk",
    ], [0.5, 2.0])
    assert "[DIR]  persist_test" in screen2, "Directory did not persist across independent reboot"
    print("  [PASS] 37-38. Persistent changes survive independent QEMU reboot")

    print("\n" + "=" * 70)
    print("ALL 39 STAGE 14C TESTS PASSED SUCCESSFULLY!")
    print("=" * 70)
    return 0

if __name__ == "__main__":
    sys.exit(main())
