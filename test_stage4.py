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

def run_qemu_session(interaction_plan, boot_wait=1.2):
    """
    Runs a QEMU instance and performs multiple phases of sending keys and sampling VGA screen.
    interaction_plan is a list of tuples:
      (key_sequence, wait_after_keys_sec)
    Returns the list of screen rows for each phase.
    """
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(boot_wait)

    phase_screens = []

    # Helper to dump VGA buffer
    def dump_screen():
        p.stdin.write("xp /4000xb 0xb8000\n")
        p.stdin.flush()
        # Read monitor output until prompt
        # Rather than tricky parsing, we can send all commands and dump at the end or interact via pipes
        pass

    # For reliability with QEMU monitor stdio, we send keys and then final dump or use communicate
    return p

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

def parse_ticks(screen_text):
    """Extract all Ticks: <number> values found in the screen."""
    matches = re.findall(r"Ticks:\s*(\d+)", screen_text)
    return [int(m) for m in matches]

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
    print("STAGE 4 COMPREHENSIVE AUTOMATED VERIFICATION")
    print("==================================================")

    passed_count = 0
    total_count = 9

    # Test 1 & Test 2: Boot & Timer Initialization Milestone
    print("\n[TEST 1 & 2] Verifying Boot and Timer Initialization Checkpoints...")
    rows = run_qemu_test([], wait_time=0.3, boot_wait=1.2)
    screen_text = "\n".join(rows)

    t1_pass = "MyOS - Educational x86-64 Kernel" in screen_text and "MyOS>" in screen_text
    print("Test 1 (Boot to Shell):", "PASS" if t1_pass else "FAIL")
    if t1_pass: passed_count += 1

    t2_pass = ("[OK] PIT timer initialized at 100 Hz (IRQ0 / vector 0x20)" in screen_text and
               "Stage 4 Goal Achieved: Hardware timer & timekeeping active!" in screen_text)
    print("Test 2 (Timer Milestone):", "PASS" if t2_pass else "FAIL")
    if t2_pass: passed_count += 1
    print_screen(rows, "Test 1 & 2: Boot & Banner")

    # Test 3: Timer interrupt activity (uptime command shows non-zero ticks)
    print("\n[TEST 3] Testing Timer Interrupt Activity (uptime)...")
    # Boot wait 1.2s + wait 0.5s before uptime -> should have > 100 ticks
    rows = run_qemu_test(text_to_sendkeys("uptime\n"), wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    ticks_list = parse_ticks(screen_text)
    t3_pass = len(ticks_list) > 0 and ticks_list[0] > 0 and "Uptime:" in screen_text
    print(f"Parsed Ticks: {ticks_list}")
    print("Test 3 (Active Ticks > 0):", "PASS" if t3_pass else "FAIL")
    if t3_pass: passed_count += 1
    print_screen(rows, "Test 3: uptime")

    # Test 4 & Test 8: Uptime Progression & Tick Monotonicity
    print("\n[TEST 4 & 8] Testing Uptime Progression & Tick Monotonicity...")
    # Send clear, uptime, wait, uptime
    keys = text_to_sendkeys("clear\nuptime\n")
    # We can inject a delay by sleeping between sending keystrokes
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1.2)

    for k in keys:
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.03)

    # Wait 1.0 second so PIT ticks advance substantially (~100 ticks)
    time.sleep(1.0)

    for k in text_to_sendkeys("uptime\n"):
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.03)

    time.sleep(0.4)
    out, _ = p.communicate(input="xp /4000xb 0xb8000\nq\n")

    chars = []
    for line in out.splitlines():
        if ":" in line:
            for b in line.split(":")[1].split():
                try:
                    chars.append(int(b, 16))
                except ValueError:
                    pass
    prog_rows = []
    for r in range(25):
        row_str = ""
        for c in range(80):
            idx = (r * 80 + c) * 2
            if idx < len(chars):
                byte_val = chars[idx]
                row_str += chr(byte_val) if 32 <= byte_val < 127 else " "
        prog_rows.append(row_str.rstrip())

    prog_text = "\n".join(prog_rows)
    prog_ticks = parse_ticks(prog_text)
    print(f"Progression Ticks Sampled: {prog_ticks}")

    t4_pass = len(prog_ticks) >= 2 and prog_ticks[1] > prog_ticks[0]
    print("Test 4 (Tick Progression):", "PASS" if t4_pass else "FAIL")
    if t4_pass: passed_count += 1

    t8_pass = len(prog_ticks) >= 2 and all(prog_ticks[i] >= prog_ticks[i-1] for i in range(1, len(prog_ticks)))
    print("Test 8 (Tick Monotonicity):", "PASS" if t8_pass else "FAIL")
    if t8_pass: passed_count += 1
    print_screen(prog_rows, "Test 4 & 8: Progression & Monotonicity")

    # Test 5: Keyboard regression while timer is running
    print("\n[TEST 5] Testing Keyboard Input under continuous timer interrupts...")
    # Send characters, backspace, numbers, enter
    kbd_keys = text_to_sendkeys("echo Timer+Keyboard\n")
    rows = run_qemu_test(kbd_keys, wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t5_pass = "Timer+Keyboard" in screen_text
    print("Test 5 (Keyboard Under Timer):", "PASS" if t5_pass else "FAIL")
    if t5_pass: passed_count += 1
    print_screen(rows, "Test 5: Keyboard Regression")

    # Test 6: Shell command regression
    print("\n[TEST 6] Testing Shell Commands Regression (help, about, echo, clear, uptime)...")
    shell_keys = text_to_sendkeys("clear\nhelp\nabout\necho test\nuptime\n")
    rows = run_qemu_test(shell_keys, wait_time=0.6, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t6_pass = ("Available commands:" in screen_text and
               "uptime" in screen_text and
               "Timer: PIT Channel 0" in screen_text and
               "test" in screen_text and
               "Uptime:" in screen_text)
    print("Test 6 (Shell Regression):", "PASS" if t6_pass else "FAIL")
    if t6_pass: passed_count += 1
    print_screen(rows, "Test 6: Shell Regression")

    # Test 7: Long-running stability (let timer interrupt fire for 3.0 seconds)
    print("\n[TEST 7] Testing Long-Running Stability (continuous IRQ0 interrupts for 3.0s)...")
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1.0)
    # Let it run freely with timer interrupts for 3.0 seconds
    time.sleep(3.0)
    # Send echo alive
    for k in text_to_sendkeys("echo SystemAlive\n"):
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.03)
    time.sleep(0.4)
    out, _ = p.communicate(input="xp /4000xb 0xb8000\nq\n")
    chars = []
    for line in out.splitlines():
        if ":" in line:
            for b in line.split(":")[1].split():
                try:
                    chars.append(int(b, 16))
                except ValueError:
                    pass
    stab_rows = []
    for r in range(25):
        row_str = ""
        for c in range(80):
            idx = (r * 80 + c) * 2
            if idx < len(chars):
                byte_val = chars[idx]
                row_str += chr(byte_val) if 32 <= byte_val < 127 else " "
        stab_rows.append(row_str.rstrip())
    stab_text = "\n".join(stab_rows)
    t7_pass = "SystemAlive" in stab_text
    print("Test 7 (Long-Running Stability):", "PASS" if t7_pass else "FAIL")
    if t7_pass: passed_count += 1
    print_screen(stab_rows, "Test 7: Long-Running Stability")

    # Test 9: Approximate frequency rate check
    print("\n[TEST 9] Measuring Approximate Timer Tick Frequency...")
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1.2)
    # Sample 1: uptime
    for k in text_to_sendkeys("clear\nuptime\n"):
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.03)

    # Wait exactly 1.5 wall-clock seconds
    sample_duration = 1.5
    time.sleep(sample_duration)

    # Sample 2: uptime
    for k in text_to_sendkeys("uptime\n"):
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.03)

    time.sleep(0.4)
    out, _ = p.communicate(input="xp /4000xb 0xb8000\nq\n")
    chars = []
    for line in out.splitlines():
        if ":" in line:
            for b in line.split(":")[1].split():
                try:
                    chars.append(int(b, 16))
                except ValueError:
                    pass
    rate_rows = []
    for r in range(25):
        row_str = ""
        for c in range(80):
            idx = (r * 80 + c) * 2
            if idx < len(chars):
                byte_val = chars[idx]
                row_str += chr(byte_val) if 32 <= byte_val < 127 else " "
        rate_rows.append(row_str.rstrip())
    rate_text = "\n".join(rate_rows)
    rate_ticks = parse_ticks(rate_text)
    print(f"Sampled Rate Ticks: {rate_ticks}")
    if len(rate_ticks) >= 2:
        delta_ticks = rate_ticks[1] - rate_ticks[0]
        measured_hz = delta_ticks / sample_duration
        print(f"Elapsed ticks: {delta_ticks} in {sample_duration}s -> ~{measured_hz:.1f} Hz (Target: 100 Hz)")
        # Broad tolerance (40 Hz to 200 Hz) to accommodate host VM virtualization variances
        t9_pass = 40 <= measured_hz <= 200
    else:
        t9_pass = False
    print("Test 9 (Approximate Rate ~100 Hz):", "PASS" if t9_pass else "FAIL")
    if t9_pass: passed_count += 1
    print_screen(rate_rows, "Test 9: Approximate Rate")

    print("\n==================================================")
    print(f"OVERALL SUMMARY: {passed_count}/{total_count} TESTS PASSED")
    print("==================================================")
    if passed_count != total_count:
        sys.exit(1)

if __name__ == "__main__":
    main()
