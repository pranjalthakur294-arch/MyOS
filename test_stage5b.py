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

def run_qemu_test(key_sequence, wait_time=0.4, boot_wait=1.6):
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

def parse_meminfo_free_bytes(screen_text):
    match = re.search(r"Free:\s*(\d+)\s*MB\s*\((\d+)\s*bytes\)", screen_text)
    if match:
        return int(match.group(2))
    return None

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
    print("STAGE 5B COMPREHENSIVE AUTOMATED VERIFICATION")
    print("==================================================")

    passed_count = 0
    total_count = 15

    # Test 1: Boot to Shell
    print("\n[TEST 1] Verifying Boot to Shell...")
    rows = run_qemu_test([], wait_time=0.3, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t1_pass = "MyOS - Educational x86-64 Kernel" in screen_text and "MyOS>" in screen_text
    print("Test 1 (Boot to Shell):", "PASS" if t1_pass else "FAIL")
    if t1_pass: passed_count += 1
    print_screen(rows, "Test 1: Boot Banner")

    # Test 2: VMM Initialization Milestone & Banner
    print("\n[TEST 2] Verifying VMM Initialization Milestone & Banner...")
    t2_pass = ("[OK] Virtual memory manager initialized" in screen_text and
               "Stage 5B Goal Achieved: 4 KiB virtual page mapping active!" in screen_text)
    print("Test 2 (VMM Milestone Banner):", "PASS" if t2_pass else "FAIL")
    if t2_pass: passed_count += 1

    # Test 3: vmmap command
    print("\n[TEST 3] Verifying 'vmmap' command output...")
    rows = run_qemu_test(text_to_sendkeys("clear\nvmmap\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t3_pass = ("Virtual Memory:" in screen_text and
               "Paging: 4-level" in screen_text and
               "Page Size: 4096 bytes" in screen_text and
               "Identity Map: 0-1 GiB" in screen_text and
               "VMM: Active" in screen_text and
               "Test Region:" in screen_text and
               "Test Mapping: Unmapped" in screen_text)
    print("Test 3 (vmmap Output):", "PASS" if t3_pass else "FAIL")
    if t3_pass: passed_count += 1
    print_screen(rows, "Test 3: vmmap")

    # Test 4: vmtest end-to-end execution
    print("\n[TEST 4] Verifying 'vmtest' end-to-end execution...")
    rows = run_qemu_test(text_to_sendkeys("clear\nvmtest\n"), wait_time=0.5, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t4_pass = ("VMM Test:" in screen_text and
               "Mapping: OK" in screen_text and
               "Memory access: OK" in screen_text and
               "Unmap: OK" in screen_text and
               "Frame released: OK" in screen_text and
               "VMM test passed!" in screen_text)
    print("Test 4 (vmtest Complete Flow):", "PASS" if t4_pass else "FAIL")
    if t4_pass: passed_count += 1
    print_screen(rows, "Test 4: vmtest")

    # Test 5: Mapping query returns valid physical frame
    print("\n[TEST 5] Verifying Mapping Query Returns Physical Frame...")
    phys_match = re.search(r"Physical frame:\s*(0x[0-9a-fA-F]+)", screen_text)
    t5_pass = False
    phys_addr = 0
    if phys_match:
        phys_addr = int(phys_match.group(1), 16)
        t5_pass = phys_addr > 0 and (phys_addr >= 0x100000)
        print(f"Captured physical frame: 0x{phys_addr:x}")
    print("Test 5 (Valid Physical Frame):", "PASS" if t5_pass else "FAIL")
    if t5_pass: passed_count += 1

    # Test 6: Virtual address is 4 KiB aligned
    print("\n[TEST 6] Verifying Virtual Address 4 KiB Alignment...")
    virt_match = re.search(r"Virtual address:\s*(0x[0-9a-fA-F]+)", screen_text)
    t6_pass = False
    if virt_match:
        virt_addr = int(virt_match.group(1), 16)
        t6_pass = (virt_addr % 4096 == 0) and (virt_addr == 0x40000000)
        print(f"Captured virtual address: 0x{virt_addr:x}")
    print("Test 6 (Virtual Address 4 KiB Aligned):", "PASS" if t6_pass else "FAIL")
    if t6_pass: passed_count += 1

    # Test 7: Physical address is 4 KiB aligned
    print("\n[TEST 7] Verifying Physical Address 4 KiB Alignment...")
    t7_pass = (phys_addr > 0) and (phys_addr % 4096 == 0)
    print("Test 7 (Physical Frame 4 KiB Aligned):", "PASS" if t7_pass else "FAIL")
    if t7_pass: passed_count += 1

    # Test 8: Written test value read back and verified
    print("\n[TEST 8] Verifying Written Test Value Readback and Verification...")
    t8_pass = "Memory access: OK" in screen_text
    print("Test 8 (Memory Access Readback):", "PASS" if t8_pass else "FAIL")
    if t8_pass: passed_count += 1

    # Test 9: Unmap succeeds and TLB invalidated
    print("\n[TEST 9] Verifying Unmap and TLB Invalidation...")
    t9_pass = "Unmap: OK" in screen_text and "Frame released: OK" in screen_text
    print("Test 9 (Unmap and Frame Released):", "PASS" if t9_pass else "FAIL")
    if t9_pass: passed_count += 1

    # Test 10: PMM Accounting consistency after vmtest
    print("\n[TEST 10] Verifying PMM Accounting Consistency Across vmtest Calls...")
    keys = text_to_sendkeys("clear\nvmtest\nmeminfo\nvmtest\nmeminfo\n")
    rows = run_qemu_test(keys, wait_time=0.7, boot_wait=1.6)
    screen_text = "\n".join(rows)

    free_matches = list(re.finditer(r"Free:\s*(\d+)\s*MB\s*\((\d+)\s*bytes\)", screen_text))
    t10_pass = False
    if len(free_matches) >= 2:
        f1 = int(free_matches[0].group(2))
        f2 = int(free_matches[1].group(2))
        print(f"Free memory after 1st vmtest: {f1} bytes")
        print(f"Free memory after 2nd vmtest: {f2} bytes")
        # Intermediate page table frames were established in the 1st call;
        # the 2nd call must have identical free memory (no leaks!)
        t10_pass = (f1 == f2)
    print("Test 10 (PMM Consistency No Leaks):", "PASS" if t10_pass else "FAIL")
    if t10_pass: passed_count += 1
    print_screen(rows, "Test 10: Repeated vmtest Accounting")

    # Test 11: Existing identity mapping remains functional
    print("\n[TEST 11] Verifying Existing Identity Mapping Integrity...")
    rows = run_qemu_test(text_to_sendkeys("clear\necho IdentityMapActive\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t11_pass = "IdentityMapActive" in screen_text
    print("Test 11 (Identity Mapping Active):", "PASS" if t11_pass else "FAIL")
    if t11_pass: passed_count += 1

    # Test 12: Keyboard input continues working
    print("\n[TEST 12] Testing Keyboard Input under VMM...")
    rows = run_qemu_test(text_to_sendkeys("clear\necho Keyboard+VMM\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    t12_pass = "Keyboard+VMM" in screen_text
    print("Test 12 (Keyboard Input Functional):", "PASS" if t12_pass else "FAIL")
    if t12_pass: passed_count += 1

    # Test 13: Timer continues ticking
    print("\n[TEST 13] Testing Timer Ticks under VMM...")
    rows = run_qemu_test(text_to_sendkeys("clear\nuptime\n"), wait_time=0.4, boot_wait=1.6)
    screen_text = "\n".join(rows)
    tick_match = re.search(r"Ticks:\s*(\d+)", screen_text)
    t13_pass = False
    if tick_match:
        ticks = int(tick_match.group(1))
        t13_pass = ticks > 0
        print(f"Active Ticks: {ticks}")
    print("Test 13 (Timer Ticks Active):", "PASS" if t13_pass else "FAIL")
    if t13_pass: passed_count += 1

    # Test 14: Existing shell commands continue working
    print("\n[TEST 14] Testing Shell Commands Regression...")
    rows_help = run_qemu_test(text_to_sendkeys("clear\nhelp\n"), wait_time=0.4, boot_wait=1.6)
    text_help = "\n".join(rows_help)
    rows_about = run_qemu_test(text_to_sendkeys("clear\nabout\n"), wait_time=0.4, boot_wait=1.6)
    text_about = "\n".join(rows_about)

    t14_pass = ("Available commands:" in text_help and
                "vmmap" in text_help and
                "vmtest" in text_help and
                "meminfo" in text_help and
                "alloc" in text_help and
                "free" in text_help and
                "uptime" in text_help and
                "VMM: 4 KiB Virtual Page Mapping Active" in text_about)
    print("Test 14 (Shell Commands Regression):", "PASS" if t14_pass else "FAIL")
    if t14_pass: passed_count += 1
    print_screen(rows_about, "Test 14: Shell About")

    # Test 15: Long-Running Stability under continuous interrupts
    print("\n[TEST 15] Testing Long-Running Stability (2.5s with interrupts, then vmtest)...")
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1.0)
    time.sleep(2.5)  # Let ~250 timer interrupts fire
    for k in text_to_sendkeys("clear\nvmtest\n"):
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.03)
    time.sleep(0.5)
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
    t15_pass = "VMM test passed!" in stab_text
    print("Test 15 (Long-Running Stability):", "PASS" if t15_pass else "FAIL")
    if t15_pass: passed_count += 1
    print_screen(stab_rows, "Test 15: Long-Running Stability")

    print("\n==================================================")
    print(f"OVERALL SUMMARY: {passed_count}/{total_count} TESTS PASSED")
    print("==================================================")
    if passed_count != total_count:
        sys.exit(1)

if __name__ == "__main__":
    main()
