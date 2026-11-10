"""Makes a blank 8 GiB original Xbox hard disk image: no files from anyone.

    python make-blank-hdd.py out.raw

The image is raw and sparse; convert it for xemu with
    qemu-img convert -O qcow2 out.raw xbox_hdd.qcow2

Layout of a retail 8 GB disk: the config area (only the "BRFR" refurb
signature, as on a new console), then five FATX partitions, each with an
empty root folder. E: gets the empty TDATA and UDATA folders games save to.
C: has no dashboard: XBemu installs the XBL Dashboard on first start.
xemu grows the disk to 64 GiB (and adds F:) by itself when it starts.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fatx_put import Fatx  # noqa: E402

DISK_SIZE = 8 * 1024 ** 3
SECTORS_PER_CLUSTER = 32            # 16 KiB clusters
PARTITIONS = [                      # name, offset, size, volume id
    ('X', 0x00080000, 0x2EE00000, 0x5B206DDB),
    ('Y', 0x2EE80000, 0x2EE00000, 0x5B206DDB),
    ('Z', 0x5DC80000, 0x2EE00000, 0x5B206DDB),
    ('C', 0x8CA80000, 0x1F400000, 0x5B206DDB),
    ('E', 0xABE80000, 0x1312D6000, 0x5B206DDA),
]


def format_partition(f, offset, size, volume_id):
    sb = (b'FATX' + struct.pack('<IIIH', volume_id, SECTORS_PER_CLUSTER, 1, 0))
    f.seek(offset)
    f.write(sb.ljust(0x1000, b'\xff'))
    cluster = SECTORS_PER_CLUSTER * 512
    total = size // cluster
    wide = total >= 0xFFF0
    width = 4 if wide else 2
    fat_bytes = (total * width + 0xFFF) & ~0xFFF
    # Entry 0: the media byte; entry 1: the root folder, one cluster
    if wide:
        head = struct.pack('<II', 0xFFFFFFF8, 0xFFFFFFFF)
    else:
        head = struct.pack('<HH', 0xFFF8, 0xFFFF)
    f.seek(offset + 0x1000)
    f.write(head)
    # Empty root folder: every slot marked unused
    f.seek(offset + 0x1000 + fat_bytes)
    f.write(b'\xff' * cluster)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    with open(out, 'wb') as f:
        f.truncate(DISK_SIZE)
        f.seek(3 * 512)
        f.write(b'BRFR')
        for name, offset, size, vid in PARTITIONS:
            format_partition(f, offset, size, vid)
    with open(out, 'r+b') as f:
        _, offset, size, _ = PARTITIONS[-1]
        e = Fatx(f, offset, size)
        e.mkdir(1, 'TDATA')
        e.mkdir(1, 'UDATA')
    print('blank disk:', out)


if __name__ == '__main__':
    main()
