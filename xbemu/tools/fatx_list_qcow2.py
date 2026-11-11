"""List the C: and E: partitions of a qcow2 Xbox hard disk image (read only).

    python fatx_list_qcow2.py image.qcow2

Reads uncompressed qcow2 v2/v3 images (what xemu writes); unallocated clusters
read as zeros. Uses the FATX reader in fatx_put.py.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fatx_put  # noqa: E402


class Qcow2:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(72)
        if h[:4] != b'QFI\xfb':
            raise SystemExit('not a qcow2 image')
        self.cbits = struct.unpack_from('>I', h, 20)[0]
        self.size = struct.unpack_from('>Q', h, 24)[0]
        l1_size = struct.unpack_from('>I', h, 36)[0]
        l1_off = struct.unpack_from('>Q', h, 40)[0]
        self.csize = 1 << self.cbits
        self.l2_entries = self.csize // 8
        self.f.seek(l1_off)
        self.l1 = struct.unpack('>%dQ' % l1_size, self.f.read(8 * l1_size))
        self.pos = 0
        self.l2_cache = {}

    def seek(self, pos, whence=0):
        self.pos = pos if whence == 0 else self.pos + pos

    def tell(self):
        return self.pos

    def _host(self, cluster):
        l1i, l2i = divmod(cluster, self.l2_entries)
        if l1i >= len(self.l1):
            return 0
        l2_off = self.l1[l1i] & 0x00FFFFFFFFFFFE00
        if not l2_off:
            return 0
        l2 = self.l2_cache.get(l2_off)
        if l2 is None:
            self.f.seek(l2_off)
            l2 = struct.unpack('>%dQ' % self.l2_entries, self.f.read(self.csize))
            self.l2_cache[l2_off] = l2
        e = l2[l2i]
        if e & (1 << 62):
            raise SystemExit('compressed clusters are not supported')
        if e & 1 and not (e & 0x00FFFFFFFFFFFE00):
            return 0  # all-zero cluster
        return e & 0x00FFFFFFFFFFFE00

    def read(self, n):
        out = bytearray()
        while n > 0:
            cluster, inner = divmod(self.pos, self.csize)
            take = min(n, self.csize - inner)
            host = self._host(cluster)
            if host:
                self.f.seek(host + inner)
                out += self.f.read(take)
            else:
                out += bytes(take)
            self.pos += take
            n -= take
        return bytes(out)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    img = Qcow2(sys.argv[1])
    for letter, (base, size) in fatx_put.PARTS.items():
        fatx_put.walk(fatx_put.Fatx(img, base, size), 1, letter + ':\\', 0)


if __name__ == '__main__':
    main()
