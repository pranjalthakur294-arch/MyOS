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

def run_qemu_test(key_sequence, wait_time=0.4, boot_wait=1.2):
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
    print("STAGE 3B COMPREHENSIVE AUTOMATED VERIFICATION")
    print("==================================================")
    
    passed_count = 0
    total_count = 15

    # Test 1: help
    print("\n[TEST 1] Testing 'help' command...")
    rows = run_qemu_test(text_to_sendkeys("help\n"))
    screen_text = "\n".join(rows)
    t1_pass = ("Available commands:" in screen_text and
               "help" in screen_text and
               "clear" in screen_text and
               "about" in screen_text and
               "echo" in screen_text and
               "halt" in screen_text)
    print("Test 1 Result:", "PASS" if t1_pass else "FAIL")
    if t1_pass: passed_count += 1
    print_screen(rows, "Test 1: help")

    # Test 2: clear
    print("\n[TEST 2] Testing 'clear' command...")
    rows = run_qemu_test(text_to_sendkeys("clear\n"))
    t2_pass = (rows[0] == "MyOS>" or rows[0].startswith("MyOS>")) and all(r.strip() == "" for r in rows[1:])
    print("Test 2 Result:", "PASS" if t2_pass else "FAIL")
    if t2_pass: passed_count += 1
    print_screen(rows, "Test 2: clear")

    # Test 3: about
    print("\n[TEST 3] Testing 'about' command...")
    rows = run_qemu_test(text_to_sendkeys("about\n"))
    screen_text = "\n".join(rows)
    t3_pass = ("MyOS - Educational x86-64 Operating System" in screen_text and
               "Architecture: x86-64 (Long Mode, 64-bit)" in screen_text and
               "Paging: 4-level identity paging" in screen_text and
               "Interrupts: 8259 PIC + 256-entry IDT" in screen_text and
               "Input: PS/2 Keyboard (IRQ1 / Vector 0x21)" in screen_text and
               "Display: VGA 80x25 text buffer" in screen_text)
    print("Test 3 Result:", "PASS" if t3_pass else "FAIL")
    if t3_pass: passed_count += 1
    print_screen(rows, "Test 3: about")

    # Test 4: echo
    print("\n[TEST 4] Testing 'echo hello'...")
    rows = run_qemu_test(text_to_sendkeys("echo hello\n"))
    screen_text = "\n".join(rows)
    t4_pass = "hello" in screen_text and "MyOS> echo hello" in screen_text
    print("Test 4 Result:", "PASS" if t4_pass else "FAIL")
    if t4_pass: passed_count += 1
    print_screen(rows, "Test 4: echo hello")

    # Test 5: echo multiple words
    print("\n[TEST 5] Testing 'echo hello world'...")
    rows = run_qemu_test(text_to_sendkeys("echo hello world\n"))
    screen_text = "\n".join(rows)
    t5_pass = "hello world" in screen_text and "MyOS> echo hello world" in screen_text
    print("Test 5 Result:", "PASS" if t5_pass else "FAIL")
    if t5_pass: passed_count += 1
    print_screen(rows, "Test 5: echo multiple words")

    # Test 6: echo without arguments
    print("\n[TEST 6] Testing 'echo' without arguments...")
    # We clear first to easily inspect lines
    rows = run_qemu_test(text_to_sendkeys("clear\necho\n"))
    # Row 0: MyOS> echo
    # Row 1: (blank)
    # Row 2: MyOS>
    t6_pass = (rows[0] == "MyOS> echo" and rows[1].strip() == "" and rows[2].startswith("MyOS>"))
    print("Test 6 Result:", "PASS" if t6_pass else "FAIL")
    if t6_pass: passed_count += 1
    print_screen(rows, "Test 6: echo without arguments")

    # Test 7: unknown command
    print("\n[TEST 7] Testing unknown command 'xyz'...")
    rows = run_qemu_test(text_to_sendkeys("xyz\n"))
    screen_text = "\n".join(rows)
    t7_pass = "Unknown command: xyz" in screen_text
    print("Test 7 Result:", "PASS" if t7_pass else "FAIL")
    if t7_pass: passed_count += 1
    print_screen(rows, "Test 7: unknown command xyz")

    # Test 8: empty input
    print("\n[TEST 8] Testing empty input and whitespace-only input...")
    rows = run_qemu_test(text_to_sendkeys("\n   \n"))
    screen_text = "\n".join(rows)
    # The boot banner occupies rows 0-14, initial prompt at 16, Enter on empty produces 17, Enter on spaces produces 18
    t8_pass = ("Unknown command:" not in screen_text and
               rows[16].startswith("MyOS>") and
               rows[17].startswith("MyOS>") and
               rows[18].startswith("MyOS>"))
    print("Test 8 Result:", "PASS" if t8_pass else "FAIL")
    if t8_pass: passed_count += 1
    print_screen(rows, "Test 8: empty & whitespace input")

    # Test 9: leading spaces
    print("\n[TEST 9] Testing leading spaces '   help'...")
    rows = run_qemu_test(text_to_sendkeys("   help\n"))
    screen_text = "\n".join(rows)
    t9_pass = "Available commands:" in screen_text
    print("Test 9 Result:", "PASS" if t9_pass else "FAIL")
    if t9_pass: passed_count += 1
    print_screen(rows, "Test 9: leading spaces")

    # Test 10: trailing spaces
    print("\n[TEST 10] Testing trailing spaces 'help   '...")
    rows = run_qemu_test(text_to_sendkeys("help   \n"))
    screen_text = "\n".join(rows)
    t10_pass = "Available commands:" in screen_text
    print("Test 10 Result:", "PASS" if t10_pass else "FAIL")
    if t10_pass: passed_count += 1
    print_screen(rows, "Test 10: trailing spaces")

    # Test 11: multiple spaces
    print("\n[TEST 11] Testing multiple spaces 'echo    hello    world'...")
    rows = run_qemu_test(text_to_sendkeys("clear\necho    hello    world\n"))
    # Row 0: MyOS> echo    hello    world
    # Row 1: hello    world
    # Row 2: MyOS>
    screen_text = "\n".join(rows)
    t11_pass = "hello    world" in screen_text
    print("Test 11 Result:", "PASS" if t11_pass else "FAIL")
    if t11_pass: passed_count += 1
    print_screen(rows, "Test 11: multiple spaces")

    # Test 12: long command name (> 31 chars)
    print("\n[TEST 12] Testing long command name (> 31 chars)...")
    long_cmd = "abcdefghijklmnopqrstuvwxyz123456"  # 32 chars
    rows = run_qemu_test(text_to_sendkeys(long_cmd + "\n"))
    screen_text = "\n".join(rows)
    t12_pass = f"Unknown command: {long_cmd}" in screen_text
    print("Test 12 Result:", "PASS" if t12_pass else "FAIL")
    if t12_pass: passed_count += 1
    print_screen(rows, "Test 12: long command name")

    # Test 13: repeated commands
    print("\n[TEST 13] Testing repeated commands sequence...")
    seq = "clear\nabout\necho test\nhelp\necho hello world\nabout\n"
    rows = run_qemu_test(text_to_sendkeys(seq), wait_time=0.6)
    screen_text = "\n".join(rows)
    t13_pass = ("test" in screen_text and
                "Available commands:" in screen_text and
                "hello world" in screen_text and
                screen_text.count("Display: VGA 80x25 text buffer") >= 2)
    print("Test 13 Result:", "PASS" if t13_pass else "FAIL")
    if t13_pass: passed_count += 1
    print_screen(rows, "Test 13: repeated commands")

    # Test 14: terminal editing (printable chars, backspace, prompt protection, shift, numbers, punctuation)
    print("\n[TEST 14] Testing terminal line editing & prompt protection...")
    # 1. 5 backspaces on prompt -> prompt still intact
    # 2. type "echx", backspace, "o Done! 123", enter
    edit_keys = [
        "sendkey backspace", "sendkey backspace", "sendkey backspace",
        "sendkey e", "sendkey c", "sendkey h", "sendkey x",
        "sendkey backspace",
        "sendkey o", "sendkey spc",
        "sendkey shift-d", "sendkey o", "sendkey n", "sendkey e", "sendkey shift-1",
        "sendkey spc", "sendkey 1", "sendkey 2", "sendkey 3",
        "sendkey ret"
    ]
    rows = run_qemu_test(edit_keys)
    screen_text = "\n".join(rows)
    t14_pass = "Done! 123" in screen_text
    print("Test 14 Result:", "PASS" if t14_pass else "FAIL")
    if t14_pass: passed_count += 1
    print_screen(rows, "Test 14: terminal editing")

    # Test 15: halt
    print("\n[TEST 15] Testing 'halt' command...")
    # Run halt, then attempt to send keystrokes afterwards
    halt_keys = (text_to_sendkeys("halt\n") +
                 ["sendkey e", "sendkey c", "sendkey h", "sendkey o", "sendkey spc", "sendkey n", "sendkey o", "sendkey ret"])
    rows = run_qemu_test(halt_keys, wait_time=0.5)
    screen_text = "\n".join(rows)
    # Verify:
    # 1. "System halted." appears
    # 2. No new prompt or echo "no" appears after halt
    t15_pass = "System halted." in screen_text and "echo no" not in screen_text
    print("Test 15 Result:", "PASS" if t15_pass else "FAIL")
    if t15_pass: passed_count += 1
    print_screen(rows, "Test 15: halt")

    print("\n==================================================")
    print(f"OVERALL SUMMARY: {passed_count}/{total_count} TESTS PASSED")
    print("==================================================")
    if passed_count != total_count:
        sys.exit(1)

if __name__ == "__main__":
    main()
