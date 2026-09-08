import subprocess
import time

def run_qemu_test(key_sequence, wait_time=0.5):
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1.6)

    for k in key_sequence:
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.04)

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

def main():
    print("\n--- RUNNING STAGE 3A AUTOMATED VERIFICATION ---")

    # Scenario A: Prompt Protection, Typing, Backspace, Multiple Lines, and Enter
    # 1. Start: prompt should be "MyOS> "
    # 2. Press 5 Backspaces immediately -> prompt MUST NOT be erased
    # 3. Type "hello world"
    # 4. Type "X", then Backspace -> should be "hello world"
    # 5. Press Enter -> moves to new line with fresh "MyOS> "
    # 6. Type "first", Enter, "second", Enter, "third"
    # 7. Enter, then shifted text "Shifted !@# 123"
    keys = [
        # Test 5: Prompt protection (5 backspaces on empty line)
        "sendkey backspace", "sendkey backspace", "sendkey backspace", "sendkey backspace", "sendkey backspace",
        # Test 2 & 3: Normal typing & spaces
        "sendkey h", "sendkey e", "sendkey l", "sendkey l", "sendkey o",
        "sendkey spc",
        "sendkey w", "sendkey o", "sendkey r", "sendkey l", "sendkey d",
        # Test 4: Backspace functionality
        "sendkey shift-x", "sendkey backspace",
        # Test 6: Enter key
        "sendkey ret",
        # Test 7: Multiple lines
        "sendkey f", "sendkey i", "sendkey r", "sendkey s", "sendkey t",
        "sendkey ret",
        "sendkey s", "sendkey e", "sendkey c", "sendkey o", "sendkey n", "sendkey d",
        "sendkey ret",
        "sendkey t", "sendkey h", "sendkey i", "sendkey r", "sendkey d",
        "sendkey ret",
        # Test 9: Shifted symbols and numbers
        "sendkey shift-1", "sendkey shift-2", "sendkey shift-3",
        "sendkey spc",
        "sendkey 1", "sendkey 2", "sendkey 3"
    ]

    rows = run_qemu_test(keys, wait_time=0.5)

    print("=" * 70)
    print("VGA SCREEN BUFFER DUMP (SCENARIO A):")
    print("=" * 70)
    for idx, r in enumerate(rows):
        if r.strip():
            print(f"[{idx:02d}] {r}")
    print("=" * 70)

    # Scenario B: Test 8 - Buffer Boundary (Attempt to type 280 characters)
    print("\n--- RUNNING SCENARIO B: BUFFER BOUNDARY (280 KEYSTROKES) ---")
    flood_keys = ["sendkey a"] * 280 + ["sendkey ret", "sendkey o", "sendkey k"]
    rows_b = run_qemu_test(flood_keys, wait_time=0.5)

    print("=" * 70)
    print("VGA SCREEN BUFFER DUMP (SCENARIO B - BUFFER BOUNDARY):")
    print("=" * 70)
    for idx, r in enumerate(rows_b):
        if r.strip():
            print(f"[{idx:02d}] {r}")
    print("=" * 70)

if __name__ == "__main__":
    main()
