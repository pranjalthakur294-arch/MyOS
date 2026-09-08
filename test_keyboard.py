import subprocess
import time

def read_vga_lines(p):
    out, _ = p.communicate(input="xp /4000xb 0xb8000\nq\n")
    chars = []
    for line in out.splitlines():
        if ":" in line:
            parts = line.split(":")[1].split()
            for b in parts:
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
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1.6)

    # Comprehensive test sequence:
    # 1. Letters: 'hello'
    # 2. Shifted symbols: 'shift-1' (!), 'shift-2' (@), 'shift-3' (#)
    # 3. Punctuation: ',', '.', '-'
    # 4. Tab, Enter, Backspace
    keys = [
        "sendkey h", "sendkey e", "sendkey l", "sendkey l", "sendkey o",
        "sendkey spc",
        "sendkey shift-1", "sendkey shift-2", "sendkey shift-3",
        "sendkey spc",
        "sendkey comma", "sendkey dot", "sendkey minus",
        "sendkey spc",
        "sendkey z", "sendkey backspace",
        "sendkey ret",
        "sendkey shift-m", "sendkey y", "sendkey shift-o", "sendkey shift-s"
    ]

    for k in keys:
        p.stdin.write(k + "\n")
        p.stdin.flush()
        time.sleep(0.05)

    time.sleep(0.5)

    rows = read_vga_lines(p)

    print("\n" + "=" * 70)
    print("VGA SCREEN BUFFER AFTER SENDING KEYSTROKES:")
    print("=" * 70)
    for idx, r in enumerate(rows):
        if r.strip():
            print(f"[{idx:02d}] {r}")
    print("=" * 70)

if __name__ == "__main__":
    main()
