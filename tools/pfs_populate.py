#!/usr/bin/env python3
"""
tools/pfs_populate.py - Formats and populates build/disk.img with PFS volume
containing /bin/hello and /hello ELF executables for Stage 13A.

PFS On-Disk Structure (for 32 MiB disk = 65536 sectors):
  Sector 0:       Superblock (512 bytes)
  Sectors 1-16:   Block Bitmap (16 sectors)
  Sector 17:      Inode Bitmap (1 sector)
  Sectors 18-81:  Inode Table (64 sectors = 512 inodes * 64 bytes)
  Sectors 82+:    Data blocks (65454 blocks * 512 bytes)
"""

import os
import struct
import sys

DISK_IMG = "build/disk.img"
HELLO_ELF = "build/user/hello.elf"

PFS_MAGIC = 0x50465331  # 'PFS1'
PFS_VERSION = 1
PFS_SECTOR_SIZE = 512
PFS_DIRECT_BLOCKS = 8
PFS_NO_BLOCK = 0xFFFFFFFF
PFS_ROOT_INODE = 1

PFS_INODE_FILE = 1
PFS_INODE_DIR = 2

PFS_ENTRY_FILE = 1
PFS_ENTRY_DIR = 2

TOTAL_SECTORS = 65536  # 32 MiB / 512
INODE_COUNT = 512
INODE_BITMAP_SECS = 1
INODE_TABLE_SECS = 64
BLOCK_BITMAP_SECS = 16
DATA_BLOCKS = 65454
DATA_START = 82

def populate_pfs():
    if not os.path.exists(HELLO_ELF):
        print(f"Error: {HELLO_ELF} not found. Run make first.")
        sys.exit(1)

    with open(HELLO_ELF, "rb") as f:
        elf_data = f.read()

    elf_len = len(elf_data)
    if elf_len > PFS_DIRECT_BLOCKS * PFS_SECTOR_SIZE:
        print(f"Error: {HELLO_ELF} is {elf_len} bytes, exceeding PFS limit of {PFS_DIRECT_BLOCKS * PFS_SECTOR_SIZE} bytes.")
        sys.exit(1)

    elf_blocks_needed = (elf_len + PFS_SECTOR_SIZE - 1) // PFS_SECTOR_SIZE

    # Ensure disk image exists and is at least 32 MiB
    if not os.path.exists(DISK_IMG):
        os.makedirs(os.path.dirname(DISK_IMG), exist_ok=True)
        with open(DISK_IMG, "wb") as f:
            f.truncate(TOTAL_SECTORS * PFS_SECTOR_SIZE)
    else:
        size = os.path.getsize(DISK_IMG)
        if size < TOTAL_SECTORS * PFS_SECTOR_SIZE:
            with open(DISK_IMG, "ab") as f:
                f.truncate(TOTAL_SECTORS * PFS_SECTOR_SIZE)

    # Layout of data blocks:
    # Block 0: Root directory data
    # Block 1: /bin directory data
    # Blocks 2 .. 2 + elf_blocks_needed - 1: hello.elf data
    used_blocks = 2 + elf_blocks_needed
    used_inodes = 4  # 0 (reserved), 1 (root), 2 (/bin), 3 (/hello & /bin/hello)

    # 1. Superblock (512 bytes)
    # Fields:
    # magic (4), version (4), sector_size (4), total_sectors (4),
    # block_bitmap_start (4), block_bitmap_sectors (4),
    # inode_bitmap_start (4), inode_bitmap_sectors (4),
    # inode_table_start (4), inode_table_sectors (4),
    # data_start (4), data_blocks (4),
    # free_blocks (4), inode_count (4), free_inodes (4),
    # root_inode (4), padding (448)
    sb_header = struct.pack(
        "<16I",
        PFS_MAGIC,
        PFS_VERSION,
        PFS_SECTOR_SIZE,
        TOTAL_SECTORS,
        1,                  # block_bitmap_start
        BLOCK_BITMAP_SECS,  # block_bitmap_sectors
        17,                 # inode_bitmap_start
        INODE_BITMAP_SECS,  # inode_bitmap_sectors
        18,                 # inode_table_start
        INODE_TABLE_SECS,   # inode_table_sectors
        DATA_START,         # data_start
        DATA_BLOCKS,        # data_blocks
        DATA_BLOCKS - used_blocks,  # free_blocks
        INODE_COUNT,        # inode_count
        INODE_COUNT - used_inodes,  # free_inodes
        PFS_ROOT_INODE      # root_inode
    )
    sb_sector = sb_header + b"\x00" * (PFS_SECTOR_SIZE - len(sb_header))

    # 2. Block Bitmap (16 sectors = 8192 bytes = 65536 bits)
    bb_data = bytearray(BLOCK_BITMAP_SECS * PFS_SECTOR_SIZE)
    # Set used blocks
    for b in range(used_blocks):
        bb_data[b // 8] |= (1 << (b % 8))
    # Set out-of-range trailing bits as allocated
    for b in range(DATA_BLOCKS, BLOCK_BITMAP_SECS * 4096):
        bb_data[b // 8] |= (1 << (b % 8))

    # 3. Inode Bitmap (1 sector = 512 bytes = 4096 bits)
    ib_data = bytearray(INODE_BITMAP_SECS * PFS_SECTOR_SIZE)
    # Inodes 0 (reserved), 1 (root), 2 (/bin), 3 (hello)
    for ino in range(used_inodes):
        ib_data[ino // 8] |= (1 << (ino % 8))
    # Inodes INODE_COUNT .. 4095 set as allocated
    for ino in range(INODE_COUNT, 4096):
        ib_data[ino // 8] |= (1 << (ino % 8))

    # 4. Inode Table (64 sectors = 512 inodes * 64 bytes)
    # pfs_inode_t:
    # ino (4), type (2), flags (2), size (4), direct[8] (32), reserved[5] (20) = 64 bytes
    it_data = bytearray(INODE_TABLE_SECS * PFS_SECTOR_SIZE)

    def pack_inode(ino, itype, size, direct_blocks):
        d = list(direct_blocks) + [PFS_NO_BLOCK] * (PFS_DIRECT_BLOCKS - len(direct_blocks))
        return struct.pack("<IHHI8I5I", ino, itype, 0, size, *d, 0, 0, 0, 0, 0)

    # Inode 0: Unused
    it_data[0:64] = b"\x00" * 64

    # Inode 1: Root directory
    it_data[64:128] = pack_inode(1, PFS_INODE_DIR, PFS_SECTOR_SIZE, [0])

    # Inode 2: /bin directory
    it_data[128:192] = pack_inode(2, PFS_INODE_DIR, PFS_SECTOR_SIZE, [1])

    # Inode 3: hello.elf
    hello_direct = [2 + i for i in range(elf_blocks_needed)]
    it_data[192:256] = pack_inode(3, PFS_INODE_FILE, elf_len, hello_direct)

    # 5. Directory Entries (64 bytes each):
    # inode (4), type (1), name_len (1), reserved (2), name[32] (32), padding[24] (24)
    def pack_dirent(inode, itype, name):
        name_bytes = name.encode("ascii")
        name_pad = name_bytes + b"\x00" * (32 - len(name_bytes))
        return struct.pack("<IBBH32s24s", inode, itype, len(name_bytes), 0, name_pad, b"\x00" * 24)

    # Block 0: Root directory data
    root_dir_data = bytearray(PFS_SECTOR_SIZE)
    # Entry 0: "bin" (inode 2, dir)
    root_dir_data[0:64] = pack_dirent(2, PFS_ENTRY_DIR, "bin")
    # Entry 1: "hello" (inode 3, file)
    root_dir_data[64:128] = pack_dirent(3, PFS_ENTRY_FILE, "hello")

    # Block 1: /bin directory data
    bin_dir_data = bytearray(PFS_SECTOR_SIZE)
    # Entry 0: "hello" (inode 3, file)
    bin_dir_data[0:64] = pack_dirent(3, PFS_ENTRY_FILE, "hello")

    # Blocks 2..: hello.elf data
    elf_padded = elf_data + b"\x00" * (elf_blocks_needed * PFS_SECTOR_SIZE - elf_len)

    # Write everything to disk image
    with open(DISK_IMG, "r+b") as f:
        # Sector 0: Superblock
        f.seek(0)
        f.write(sb_sector)

        # Sectors 1..16: Block bitmap
        f.seek(1 * PFS_SECTOR_SIZE)
        f.write(bb_data)

        # Sector 17: Inode bitmap
        f.seek(17 * PFS_SECTOR_SIZE)
        f.write(ib_data)

        # Sectors 18..81: Inode table
        f.seek(18 * PFS_SECTOR_SIZE)
        f.write(it_data)

        # Sector 82: Root directory data (Block 0)
        f.seek(DATA_START * PFS_SECTOR_SIZE)
        f.write(root_dir_data)

        # Sector 83: /bin directory data (Block 1)
        f.seek((DATA_START + 1) * PFS_SECTOR_SIZE)
        f.write(bin_dir_data)

        # Sector 84..: hello.elf data (Blocks 2..)
        f.seek((DATA_START + 2) * PFS_SECTOR_SIZE)
        f.write(elf_padded)

    print(f"[SUCCESS] Populated {DISK_IMG} with /bin/hello and /hello ({elf_len} bytes, {elf_blocks_needed} blocks)")

if __name__ == "__main__":
    populate_pfs()
