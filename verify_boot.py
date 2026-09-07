import subprocess
import time

def main():
    cmd = [
        "qemu-system-x86_64",
        "-kernel", "build/myos.bin",
        "-display", "none",
        "-monitor", "stdio"
    ]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1.0)
    out, err = p.communicate(input="xp /4000xb 0xb8000\nq\n")

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

    print("\n" + "=" * 70)
    print("ACTUAL QEMU VGA BUFFER OUTPUT (PHYSICAL 0xB8000):")
    print("=" * 70)
    non_empty = 0
    for idx, r in enumerate(rows):
        if r.strip():
            print(f"[{idx:02d}] {r}")
            non_empty += 1
        elif idx < 16:
            print(f"[{idx:02d}] (blank)")
    print("=" * 70)
    print(f"Total non-empty lines captured: {non_empty}\n")

    print("COLOR ATTRIBUTE VALIDATION:")
    color_samples = [
        (1, "Header Banner", 0x0B),
        (4, "Stage 1 Checkpoints", 0x0A),
        (11, "Interrupt Subsystem", 0x0A),
        (13, "Stage 2A Achieved", 0x0E),
        (15, "Halt Message", 0x0F)
    ]
    for r, label, expected in color_samples:
        attr = chars[(r * 80 + 5) * 2 + 1]
        status = "MATCH" if attr == expected else "MISMATCH"
        print(f"  Row {r:02d} ({label}): 0x{attr:02X} (Expected 0x{expected:02X}) -> [{status}]")
    print("=" * 70)

if __name__ == "__main__":
    main()
