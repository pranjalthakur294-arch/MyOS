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

def parse_meminfo(screen_text):
    total_match = re.search(r"Total:\s*(\d+)\s*MB\s*\((\d+)\s*bytes\)", screen_text)
    used_match  = re.search(r"Used:\s*(\d+)\s*MB\s*\((\d+)\s*bytes\)", screen_text)
    free_match  = re.search(r"Free:\s*(\d+)\s*MB\s*\((\d+)\s*bytes\)", screen_text)

    if total_match and used_match and free_match:
        total_b = int(total_match.group(2))
        used_b  = int(used_match.group(2))
        free_b  = int(free_match.group(2))
        return {
            'total_mb': int(total_match.group(1)),
            'total_bytes': total_b,
            'total_frames': total_b // 4096,
            'used_mb': int(used_match.group(1)),
            'used_bytes': used_b,
            'used_frames': used_b // 4096,
            'free_mb': int(free_match.group(1)),
            'free_bytes': free_b,
            'free_frames': free_b // 4096,
        }
    return None

def parse_alloc_hex(screen_text):
    matches = re.findall(r"Allocated frame:\s*(0x[0-9a-fA-F]+)", screen_text)
    return [int(m, 16) for m in matches]

def parse_free_hex(screen_text):
    matches = re.findall(r"Freed frame:\s*(0x[0-9a-fA-F]+)", screen_text)
    return [int(m, 16) for m in matches]

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
    print("STAGE 5A COMPREHENSIVE AUTOMATED VERIFICATION")
    print("==================================================")

    passed_count = 0
    total_count = 12

    # Test 1 & Test 2: Boot & PMM Milestone
    print("\n[TEST 1 & 2] Verifying Boot and PMM Initialization Checkpoints...")
    rows = run_qemu_test([], wait_time=0.3, boot_wait=1.2)
    screen_text = "\n".join(rows)

    t1_pass = "MyOS - Educational x86-64 Kernel" in screen_text and "MyOS>" in screen_text
    print("Test 1 (Boot to Shell):", "PASS" if t1_pass else "FAIL")
    if t1_pass: passed_count += 1

    t2_pass = ("[OK] Multiboot memory map parsed" in screen_text and
               "[OK] Physical memory manager initialized" in screen_text and
               "[OK] 4 KiB frame allocator ready" in screen_text and
               "Stage 5A Goal Achieved: 4 KiB frame allocator active!" in screen_text)
    print("Test 2 (PMM Milestone Banner):", "PASS" if t2_pass else "FAIL")
    if t2_pass: passed_count += 1
    print_screen(rows, "Test 1 & 2: Boot & Banner")

    # Test 3: meminfo output format and accounting invariant
    print("\n[TEST 3] Verifying meminfo output format and accounting invariant...")
    rows = run_qemu_test(text_to_sendkeys("meminfo\n"), wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    info = parse_meminfo(screen_text)

    t3_pass = False
    if info:
        bytes_eq = (info['total_bytes'] == info['used_bytes'] + info['free_bytes'])
        total_eq = (info['total_frames'] == info['used_frames'] + info['free_frames'])
        frame_size_check = "Frame Size: 4096 bytes" in screen_text
        print(f"Parsed meminfo: Total={info['total_bytes']} bytes ({info['total_frames']} frames), "
              f"Used={info['used_bytes']} bytes ({info['used_frames']} frames), "
              f"Free={info['free_bytes']} bytes ({info['free_frames']} frames)")
        t3_pass = bytes_eq and total_eq and frame_size_check and info['free_frames'] > 0
    print("Test 3 (meminfo Invariant total == used + free):", "PASS" if t3_pass else "FAIL")
    if t3_pass: passed_count += 1
    print_screen(rows, "Test 3: meminfo")

    # Test 4: alloc returns 4 KiB-aligned address
    print("\n[TEST 4] Verifying alloc command returns 4 KiB-aligned address...")
    rows = run_qemu_test(text_to_sendkeys("clear\nalloc\n"), wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    addrs = parse_alloc_hex(screen_text)
    t4_pass = len(addrs) == 1 and (addrs[0] % 4096 == 0) and addrs[0] > 0
    if addrs:
        print(f"Allocated address: hex(0x{addrs[0]:x}) dec({addrs[0]})")
    print("Test 4 (alloc 4 KiB aligned):", "PASS" if t4_pass else "FAIL")
    if t4_pass: passed_count += 1
    print_screen(rows, "Test 4: alloc")

    # Test 5: Allocation accounting (meminfo -> alloc -> meminfo)
    print("\n[TEST 5] Verifying allocation accounting (meminfo -> alloc -> meminfo)...")
    keys = text_to_sendkeys("clear\nmeminfo\nalloc\nmeminfo\n")
    rows = run_qemu_test(keys, wait_time=0.5, boot_wait=1.2)
    screen_text = "\n".join(rows)

    used_matches = list(re.finditer(r"Used:\s*(\d+)\s*MB\s*\((\d+)\s*bytes\)", screen_text))
    free_matches = list(re.finditer(r"Free:\s*(\d+)\s*MB\s*\((\d+)\s*bytes\)", screen_text))

    t5_pass = False
    if len(used_matches) >= 2 and len(free_matches) >= 2:
        u1, u2 = int(used_matches[0].group(2)), int(used_matches[1].group(2))
        f1, f2 = int(free_matches[0].group(2)), int(free_matches[1].group(2))
        print(f"Before alloc: Used={u1} bytes, Free={f1} bytes")
        print(f"After  alloc: Used={u2} bytes, Free={f2} bytes")
        t5_pass = (u2 == u1 + 4096) and (f2 == f1 - 4096)
    print("Test 5 (Accounting used+4096, free-4096):", "PASS" if t5_pass else "FAIL")
    if t5_pass: passed_count += 1
    print_screen(rows, "Test 5: Allocation Accounting")

    # Test 6: free command restores frame to free pool
    print("\n[TEST 6] Verifying free command restores frame to free pool...")
    keys = text_to_sendkeys("clear\nalloc\nfree\nmeminfo\n")
    rows = run_qemu_test(keys, wait_time=0.5, boot_wait=1.2)
    screen_text = "\n".join(rows)
    info_after = parse_meminfo(screen_text)

    clean_rows = run_qemu_test(text_to_sendkeys("meminfo\n"), wait_time=0.3, boot_wait=1.2)
    clean_info = parse_meminfo("\n".join(clean_rows))

    t6_pass = False
    if info_after and clean_info:
        print(f"Clean state: Used={clean_info['used_bytes']} bytes, Free={clean_info['free_bytes']} bytes")
        print(f"After alloc+free: Used={info_after['used_bytes']} bytes, Free={info_after['free_bytes']} bytes")
        t6_pass = ("Freed frame: 0x" in screen_text and
                   info_after['used_bytes'] == clean_info['used_bytes'] and
                   info_after['free_bytes'] == clean_info['free_bytes'])
    print("Test 6 (free restores pool):", "PASS" if t6_pass else "FAIL")
    if t6_pass: passed_count += 1
    print_screen(rows, "Test 6: free Restores Pool")

    # Test 7: Frame reuse (alloc -> free -> alloc reuses same frame)
    print("\n[TEST 7] Verifying frame reuse (alloc -> free -> alloc)...")
    keys = text_to_sendkeys("clear\nalloc\nfree\nalloc\n")
    rows = run_qemu_test(keys, wait_time=0.5, boot_wait=1.2)
    screen_text = "\n".join(rows)
    alloc_addrs = parse_alloc_hex(screen_text)
    free_addrs  = parse_free_hex(screen_text)

    t7_pass = False
    if len(alloc_addrs) == 2 and len(free_addrs) == 1:
        print(f"First alloc: 0x{alloc_addrs[0]:x}, Free: 0x{free_addrs[0]:x}, Second alloc: 0x{alloc_addrs[1]:x}")
        t7_pass = (alloc_addrs[0] == free_addrs[0] == alloc_addrs[1])
    print("Test 7 (Frame Reuse):", "PASS" if t7_pass else "FAIL")
    if t7_pass: passed_count += 1
    print_screen(rows, "Test 7: Frame Reuse")

    # Test 8: Multiple allocations return distinct, aligned addresses
    print("\n[TEST 8] Verifying multiple allocations and frees...")
    keys = text_to_sendkeys("clear\nalloc\nalloc\nalloc\nalloc\nalloc\n")
    rows = run_qemu_test(keys, wait_time=0.6, boot_wait=1.2)
    screen_text = "\n".join(rows)
    addrs = parse_alloc_hex(screen_text)

    t8_pass = False
    if len(addrs) == 5:
        print(f"Allocated 5 addresses: {[hex(a) for a in addrs]}")
        all_aligned = all(a % 4096 == 0 for a in addrs)
        all_unique = len(set(addrs)) == 5
        t8_pass = all_aligned and all_unique
    print("Test 8 (Multiple Distinct 4 KiB Frames):", "PASS" if t8_pass else "FAIL")
    if t8_pass: passed_count += 1
    print_screen(rows, "Test 8: Multiple Allocations")

    # Test 9: Reserved memory safety (allocated frames are >= 0x100000 and >= kernel_end)
    print("\n[TEST 9] Verifying reserved memory safety (frames >= 1 MiB)...")
    t9_pass = False
    if len(addrs) > 0:
        min_addr = min(addrs)
        print(f"Lowest allocated address: 0x{min_addr:x} (1 MiB is 0x100000)")
        t9_pass = min_addr >= 0x100000
    print("Test 9 (Reserved 0-1MB Safety):", "PASS" if t9_pass else "FAIL")
    if t9_pass: passed_count += 1

    # Test 10: Shell command regression (help, about, meminfo, uptime, echo, clear)
    print("\n[TEST 10] Testing Shell Commands Regression...")
    shell_keys = text_to_sendkeys("clear\nhelp\nabout\necho Stage5A-Test\nuptime\n")
    rows = run_qemu_test(shell_keys, wait_time=0.6, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t10_pass = ("Available commands:" in screen_text and
                "meminfo" in screen_text and
                "alloc" in screen_text and
                "free" in screen_text and
                "uptime" in screen_text and
                "Memory: 4 KiB Physical Frame Bitmap Allocator" in screen_text and
                "Stage5A-Test" in screen_text and
                "Uptime:" in screen_text)
    print("Test 10 (Shell Regression):", "PASS" if t10_pass else "FAIL")
    if t10_pass: passed_count += 1
    print_screen(rows, "Test 10: Shell Regression")

    # Test 11: Keyboard input functional with timer interrupts & PMM active
    print("\n[TEST 11] Testing Keyboard Input under continuous timer interrupts and PMM active...")
    kbd_keys = text_to_sendkeys("echo Timer+Kbd+PMM\n")
    rows = run_qemu_test(kbd_keys, wait_time=0.4, boot_wait=1.2)
    screen_text = "\n".join(rows)
    t11_pass = "Timer+Kbd+PMM" in screen_text
    print("Test 11 (Keyboard + Timer + PMM):", "PASS" if t11_pass else "FAIL")
    if t11_pass: passed_count += 1
    print_screen(rows, "Test 11: Keyboard Regression")

    # Test 12: Long-running stability (run 2.5 seconds with timer interrupts, then run meminfo)
    print("\n[TEST 12] Testing Long-Running Stability...")
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1.0)
    time.sleep(2.5)
    for k in text_to_sendkeys("meminfo\n"):
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
    t12_pass = "Physical Memory:" in stab_text and "Total:" in stab_text
    print("Test 12 (Long-Running Stability):", "PASS" if t12_pass else "FAIL")
    if t12_pass: passed_count += 1
    print_screen(stab_rows, "Test 12: Long-Running Stability")

    print("\n==================================================")
    print(f"OVERALL SUMMARY: {passed_count}/{total_count} TESTS PASSED")
    print("==================================================")
    if passed_count != total_count:
        sys.exit(1)

if __name__ == "__main__":
    main()
