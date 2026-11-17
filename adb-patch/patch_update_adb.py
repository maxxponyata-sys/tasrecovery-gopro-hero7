#!/usr/bin/env python3
"""Enable ADB (and disable MTP) in a GoPro HERO7 Silver/White firmware update package.

    python3 patch_update_adb.py UPDATE.zip [-o OUTDIR] [--no-gpcam-fix]

Input : the UPDATE.zip you put on the SD card (CAMFW.zip + CAMVER.bin + FWUPD.txt) - a full
        (non-incremental) package for the HERO7 Silver/White ("Tasmania", APQ8053).
Output: OUTDIR/UPDATE/  (copy this folder to the root of the SD card) and OUTDIR/UPDATE.zip.

What changes - nothing else. Every file is patched *in place* inside the ext4 system image, inside the
blocks it already owns, so system.transfer.list stays valid and every other zip entry is copied byte for byte:
  /usr/bin/start_mtp.sh     gpcam runs it when it wants MTP; now it switches USB to ADB-only and keeps it there
  /etc/rc5.d/S99GoPro.sh    + start_mtp.sh adbwatch at boot (re-asserts ADB every 5 s)
  /etc/rc5.d/S00GoPro.sh    + mkdir /tmp/class/android_usb/android0 before gpcam starts      (gpcam fix)
  /usr/bin/gpcam            its /sys/class/android_usb/... strings -> /tmp/class/android_usb/... so it can
                            no longer flip USB back to MTP (that made adb drop and the camera reboot)
  updater-script            range_sha1() checks of the system partition are recomputed
  CAMVER.bin                CRC32 of the new CAMFW.zip at offset 0x24

No GoPro code is redistributed: the script only edits the package you already have.
Written for TasRecovery (https://github.com/maxxponyata-sys/tasrecovery-gopro-hero7). Use at your own risk.
"""
import argparse, hashlib, io, os, re, shutil, struct, sys, tempfile, zipfile, zlib

BLOCK = 4096

# ------------------------------------------------------------------ new file contents
START_MTP = b'''#!/bin/sh
# ADB-only USB (MTP disabled) - patched by patch_update_adb.py
# gpcam runs this when it wants MTP; S99GoPro.sh runs it with "adbwatch" at boot.
U=/sys/class/android_usb/android0

start_adbd() {
    mkdir -p /dev/usb-ffs/adb
    grep -q ' /dev/usb-ffs/adb ' /proc/mounts || \\
        mount -o uid=2000,gid=2000 -t functionfs adb /dev/usb-ffs/adb
    [ -d $U/f_ffs ] && echo adb > $U/f_ffs/aliases
    pidof adbd >/dev/null || start-stop-daemon -S -b -a /sbin/adbd
}

adb_only() {
    [ -d $U ] || return 0
    pkill gpMtpServer 2>/dev/null
    start_adbd
    # the kernel reports the adb function as "ffs" (FunctionFS)
    case "$(cat $U/functions 2>/dev/null)" in
        adb|ffs) [ "$(cat $U/enable 2>/dev/null)" = "1" ] && return 0 ;;
    esac
    echo 0    > $U/enable
    echo 18D1 > $U/idVendor
    echo 4EE7 > $U/idProduct
    echo adb  > $U/functions
    sleep 1
    echo 1    > $U/enable
    logger -t gp_adb "USB switched to ADB-only"
}

adb_only
if [ "$1" = "adbwatch" ]; then
    while true; do
        sleep 5
        adb_only
    done
fi
exit 0
'''
S99_ADD = b'\n# ADB-only USB (MTP disabled) - patched by patch_update_adb.py\n/bin/sh /usr/bin/start_mtp.sh adbwatch &\n'
S00_ADD = b'# gpcam USB writes go to a dummy dir (patch_update_adb.py)\nmkdir -p /tmp/class/android_usb/android0\n'
GPCAM_OLD = b'/sys/class/android_usb/android0/'
GPCAM_NEW = b'/tmp/class/android_usb/android0/'
MARK = b'patch_update_adb.py'


def die(msg):
    sys.exit('error: ' + msg)


# ------------------------------------------------------------------ transfer list / system image
def parse_ranges(s):
    a = list(map(int, s.split(',')))
    assert a[0] == len(a) - 1
    return [(a[i], a[i + 1]) for i in range(1, len(a), 2)]


def read_transfer_list(text):
    lines = text.splitlines()
    version = int(lines[0])
    body = lines[4:] if version >= 2 else lines[2:]
    cmds = []
    for ln in body:
        if not ln.strip():
            continue
        op, *rest = ln.split(' ')
        if op not in ('new', 'zero', 'erase'):
            die('system.transfer.list contains "%s" - incremental packages are not supported, use a full UPDATE.zip' % op)
        cmds.append((op, parse_ranges(rest[0])))
    return version, cmds


def build_image(cmds, dat_stream, path):
    total = max(e for _, rs in cmds for _, e in rs)
    with open(path, 'wb') as img:
        img.truncate(total * BLOCK)
        for op, rs in cmds:
            if op != 'new':
                continue
            for s, e in rs:
                img.seek(s * BLOCK)
                n = (e - s) * BLOCK
                while n:
                    d = dat_stream.read(min(n, 1 << 22))
                    if not d:
                        die('system.new.dat is shorter than system.transfer.list says')
                    img.write(d)
                    n -= len(d)
    return total


# ------------------------------------------------------------------ minimal ext4 reader / in-place writer
class Ext4:
    def __init__(self, f):
        self.f = f
        f.seek(1024)
        sb = f.read(1024)
        if struct.unpack_from('<H', sb, 56)[0] != 0xEF53:
            die('system image is not ext4')
        self.bs = 1024 << struct.unpack_from('<I', sb, 24)[0]
        self.ipg = struct.unpack_from('<I', sb, 40)[0]
        self.isize = struct.unpack_from('<H', sb, 88)[0]
        incompat, ro = struct.unpack_from('<I', sb, 96)[0], struct.unpack_from('<I', sb, 100)[0]
        if ro & 0x400:
            die('ext4 metadata_csum is enabled - in-place patching would need checksum updates (not supported)')
        if incompat & 0x80:
            die('64-bit ext4 is not supported')
        self.first = struct.unpack_from('<I', sb, 20)[0]

    def blk(self, n):
        self.f.seek(n * self.bs)
        return self.f.read(self.bs)

    def inode_pos(self, ino):
        g, i = divmod(ino - 1, self.ipg)
        self.f.seek((self.first + 1) * self.bs + g * 32)
        table = struct.unpack_from('<I', self.f.read(32), 8)[0]
        return table * self.bs + i * self.isize

    def inode(self, ino):
        self.f.seek(self.inode_pos(ino))
        return self.f.read(self.isize)

    def extents(self, raw):
        """(file block, disk block, count) for an extent-mapped inode"""
        out = []

        def walk(node):
            magic, entries, _, depth = struct.unpack_from('<HHHH', node, 0)
            if magic != 0xF30A:
                die('unexpected extent header')
            for k in range(entries):
                o = 12 + 12 * k
                if depth == 0:
                    lblk, ln, hi, lo = struct.unpack_from('<IHHI', node, o)
                    if ln > 32768:
                        ln -= 32768
                    out.append((lblk, (hi << 32) | lo, ln))
                else:
                    _, lo, hi = struct.unpack_from('<IIH', node, o)
                    walk(self.blk((hi << 32) | lo))
        if not struct.unpack_from('<I', raw, 32)[0] & 0x80000:
            die('file is not extent-mapped')
        walk(raw[40:100])
        return sorted(out)

    def size(self, raw):
        return struct.unpack_from('<I', raw, 4)[0] | (struct.unpack_from('<I', raw, 108)[0] << 32)

    def read_file(self, ino):
        raw = self.inode(ino)
        sz, data = self.size(raw), bytearray()
        for lblk, pblk, n in self.extents(raw):
            start = lblk * self.bs
            if len(data) < start:
                data += bytes(start - len(data))     # hole in a sparse file
            del data[start:]
            data += b''.join(self.blk(pblk + i) for i in range(n))
        return bytes(data[:sz])

    def lookup(self, path):
        ino = 2
        for part in path.strip('/').split('/'):
            data, found = self.read_file(ino), None
            o = 0
            while o + 8 <= len(data):
                d_ino, rec, nlen, _ = struct.unpack_from('<IHBB', data, o)
                if rec < 8:
                    break
                if d_ino and data[o + 8:o + 8 + nlen] == part.encode():
                    found = d_ino
                    break
                o += rec
            if not found:
                die('not found in the system image: ' + path)
            ino = found
            mode = struct.unpack_from('<H', self.inode(ino), 0)[0]
            if mode & 0xF000 == 0xA000:
                die('%s is a symlink - unexpected layout' % path)
        return ino

    def blocks_of(self, ino):
        return [p + i for _, p, n in self.extents(self.inode(ino)) for i in range(n)]

    def write_file(self, ino, content):
        """rewrite a file inside the blocks it already owns; returns the changed block numbers"""
        blocks = self.blocks_of(ino)
        if len(content) > len(blocks) * self.bs:
            die('new content does not fit into the existing blocks (inode %d)' % ino)
        changed = []
        for i, b in enumerate(blocks):
            chunk = content[i * self.bs:(i + 1) * self.bs]
            chunk = chunk + bytes(self.bs - len(chunk))
            if self.blk(b) != chunk:
                self.f.seek(b * self.bs)
                self.f.write(chunk)
                changed.append(b)
        pos = self.inode_pos(ino)
        self.f.seek(pos + 4)
        self.f.write(struct.pack('<I', len(content) & 0xffffffff))
        self.f.seek(pos + 108)
        self.f.write(struct.pack('<I', len(content) >> 32))
        changed.append(pos // self.bs)
        return changed


# ------------------------------------------------------------------ zip helpers
def rebuild_camfw(src_path, out_path, replacements):
    """copy every entry of src byte-for-byte except those in replacements {name: bytes or file path}"""
    z = zipfile.ZipFile(src_path)
    infos = z.infolist()
    with open(src_path, 'rb') as f:
        f.seek(-22, 2)
        eocd = bytearray(f.read(22))
        if eocd[:4] != b'PK\x05\x06':
            die('CAMFW.zip has a zip comment or zip64 end record - not supported')
        cd_size, cd_off = struct.unpack_from('<II', eocd, 12)
        f.seek(cd_off)
        cd_raw = f.read(cd_size)
    offs = sorted(i.header_offset for i in infos) + [cd_off]
    nxt = {o: offs[k + 1] for k, o in enumerate(offs[:-1])}
    new_off, newmeta = {}, {}
    with open(src_path, 'rb') as f, open(out_path, 'wb') as o:
        for i in infos:
            new_off[i.filename] = o.tell()
            f.seek(i.header_offset)
            if i.filename not in replacements:
                n = nxt[i.header_offset] - i.header_offset
                while n:
                    d = f.read(min(n, 1 << 22)); o.write(d); n -= len(d)
                continue
            lh = bytearray(f.read(30))
            nl, el = struct.unpack_from('<HH', lh, 26)
            name_extra = f.read(nl + el)
            src = replacements[i.filename]
            comp = zlib.compressobj(6, zlib.DEFLATED, -15)
            crc = usize = 0
            tmp = io.BytesIO() if isinstance(src, (bytes, bytearray)) else tempfile.TemporaryFile()
            stream = io.BytesIO(src) if isinstance(src, (bytes, bytearray)) else open(src, 'rb')
            with stream:
                while True:
                    d = stream.read(1 << 22)
                    if not d:
                        break
                    crc = zlib.crc32(d, crc); usize += len(d); tmp.write(comp.compress(d))
            tmp.write(comp.flush())
            csize = tmp.tell()
            struct.pack_into('<HH', lh, 6, struct.unpack_from('<H', lh, 6)[0] & ~0x8, 8)   # no data descriptor, deflate
            struct.pack_into('<III', lh, 14, crc, csize, usize)
            o.write(lh + name_extra)
            tmp.seek(0)
            shutil.copyfileobj(tmp, o, 1 << 22)
            newmeta[i.filename] = (crc, csize, usize)
        cd_new, p = bytearray(), 0
        while p < len(cd_raw):
            nl, el, cl = struct.unpack_from('<HHH', cd_raw, p + 28)
            rec = bytearray(cd_raw[p:p + 46 + nl + el + cl])
            name = bytes(rec[46:46 + nl]).decode('cp437')
            if name in newmeta:
                struct.pack_into('<H', rec, 8, struct.unpack_from('<H', rec, 8)[0] & ~0x8)
                struct.pack_into('<H', rec, 10, 8)
                struct.pack_into('<III', rec, 16, *newmeta[name])
            struct.pack_into('<I', rec, 42, new_off[name])
            cd_new += rec
            p += len(rec)
        cdo = o.tell()
        o.write(cd_new)
        struct.pack_into('<II', eocd, 12, len(cd_new), cdo)
        o.write(eocd)


def crc32_file(path):
    c = 0
    with open(path, 'rb') as f:
        for d in iter(lambda: f.read(1 << 22), b''):
            c = zlib.crc32(d, c)
    return c


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description='Enable ADB / disable MTP in a GoPro HERO7 Silver/White UPDATE.zip')
    ap.add_argument('update_zip')
    ap.add_argument('-o', '--outdir', default='patched')
    ap.add_argument('--no-gpcam-fix', action='store_true', help='do not redirect gpcam\'s android_usb paths')
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    work = tempfile.mkdtemp(prefix='gpadb-', dir=a.outdir)
    try:
        outer = zipfile.ZipFile(a.update_zip)
        names = outer.namelist()
        for need in ('CAMFW.zip', 'CAMVER.bin', 'FWUPD.txt'):
            if need not in names:
                die('%s is missing from %s' % (need, a.update_zip))
        camfw = os.path.join(work, 'CAMFW.zip')
        with outer.open('CAMFW.zip') as s, open(camfw, 'wb') as d:
            shutil.copyfileobj(s, d, 1 << 22)
        camver = bytearray(outer.read('CAMVER.bin'))
        fwupd = outer.read('FWUPD.txt')
        if camver[:4] != b'WFPG' or len(camver) < 0x28:
            die('CAMVER.bin is not a GoPro WFPG header')
        if struct.unpack_from('<I', camver, 0x24)[0] != crc32_file(camfw):
            die('CAMVER.bin CRC does not match CAMFW.zip - the input package is damaged or of another format')
        print('firmware %s, camera ids %s' % (camver[0x14:0x24].rstrip(b'\0').decode(errors='replace'),
                                              list(struct.unpack_from('<4I', camver, 4))))

        z = zipfile.ZipFile(camfw)
        version, cmds = read_transfer_list(z.read('system.transfer.list').decode())
        img_path = os.path.join(work, 'system.img')
        print('rebuilding the system image (transfer list v%d) ...' % version)
        with z.open('system.new.dat') as dat:
            nblocks = build_image(cmds, dat, img_path)
        new_ranges = [r for op, rs in cmds if op == 'new' for r in rs]

        changed = []
        with open(img_path, 'r+b') as f:
            fs = Ext4(f)
            if fs.bs != BLOCK:
                die('unexpected ext4 block size %d' % fs.bs)
            ino = fs.lookup('/usr/bin/start_mtp.sh')
            if MARK in fs.read_file(ino):
                die('this package is already patched')
            changed += fs.write_file(ino, START_MTP)
            print('  patched /usr/bin/start_mtp.sh')
            ino = fs.lookup('/etc/rc5.d/S99GoPro.sh')
            changed += fs.write_file(ino, fs.read_file(ino).rstrip(b'\n') + b'\n' + S99_ADD)
            print('  patched /etc/rc5.d/S99GoPro.sh')
            if not a.no_gpcam_fix:
                ino = fs.lookup('/etc/rc5.d/S00GoPro.sh')
                s00 = fs.read_file(ino)
                if s00.count(b'/usr/bin/gpcam &') != 1:
                    die('S00GoPro.sh does not start gpcam the expected way - rerun with --no-gpcam-fix')
                changed += fs.write_file(ino, s00.replace(b'/usr/bin/gpcam &', S00_ADD + b'/usr/bin/gpcam &'))
                print('  patched /etc/rc5.d/S00GoPro.sh')
                ino = fs.lookup('/usr/bin/gpcam')
                hits = 0
                for b in fs.blocks_of(ino):
                    d = bytearray(fs.blk(b))
                    if GPCAM_OLD in d:
                        hits += d.count(GPCAM_OLD)
                        f.seek(b * BLOCK); f.write(bytes(d).replace(GPCAM_OLD, GPCAM_NEW)); changed.append(b)
                # strings split across a block boundary are not expected (they are short); report the count
                print('  patched /usr/bin/gpcam (%d android_usb paths)' % hits)
                if hits == 0:
                    die('no android_usb paths found in gpcam - rerun with --no-gpcam-fix')

        changed = sorted(set(changed))
        outside = [b for b in changed if not any(s <= b < e for s, e in new_ranges)]
        if outside:
            die('changed blocks %s are outside the "new" ranges of system.transfer.list' % outside[:8])
        print('  %d blocks changed, all inside the transfer list' % len(changed))

        # new system.new.dat in transfer-list order
        dat_path = os.path.join(work, 'system.new.dat')
        with open(img_path, 'rb') as img, open(dat_path, 'wb') as o:
            for s, e in new_ranges:
                img.seek(s * BLOCK)
                n = (e - s) * BLOCK
                while n:
                    d = img.read(min(n, 1 << 22)); o.write(d); n -= len(d)

        # updater-script: recompute range_sha1 checks of the system partition
        repl = {'system.new.dat': dat_path}
        us_name = 'META-INF/com/google/android/updater-script'
        us = z.read(us_name).decode('utf-8', 'replace')
        checks = list(re.finditer(r'range_sha1\("([^"]*system[^"]*)",\s*"([0-9,]+)"\)\s*==\s*"([0-9a-f]{40})"', us))
        if checks:
            with open(img_path, 'rb') as img:
                for m in checks:
                    h = hashlib.sha1()
                    for s, e in parse_ranges(m.group(2)):
                        img.seek(s * BLOCK); h.update(img.read((e - s) * BLOCK))
                    us = us.replace(m.group(0), m.group(0)[:-42] + '"' + h.hexdigest() + '"')
            repl[us_name] = us.encode()
            print('  updater-script: %d range_sha1 check(s) recomputed' % len(checks))
        if 'block_image_verify' in us:
            print('  warning: updater-script uses block_image_verify - not touched')

        out_camfw = os.path.join(work, 'CAMFW.new.zip')
        rebuild_camfw(camfw, out_camfw, repl)
        zipfile.ZipFile(out_camfw).testzip()
        struct.pack_into('<I', camver, 0x24, crc32_file(out_camfw))

        upd = os.path.join(a.outdir, 'UPDATE')
        os.makedirs(upd, exist_ok=True)
        shutil.move(out_camfw, os.path.join(upd, 'CAMFW.zip'))
        open(os.path.join(upd, 'CAMVER.bin'), 'wb').write(camver)
        open(os.path.join(upd, 'FWUPD.txt'), 'wb').write(fwupd)
        with zipfile.ZipFile(os.path.join(a.outdir, 'UPDATE.zip'), 'w', zipfile.ZIP_DEFLATED) as zo:
            for n in names:
                if n in ('CAMFW.zip', 'CAMVER.bin', 'FWUPD.txt'):
                    zo.write(os.path.join(upd, n), n)
        print('done:\n  %s  <- copy this folder to the root of the SD card\n  %s'
              % (upd, os.path.join(a.outdir, 'UPDATE.zip')))
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
