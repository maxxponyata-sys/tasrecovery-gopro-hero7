"""Pack TasRecovery: rootfs dir -> newc cpio.gz -> Android boot image v0 with the stock GoPro kernel."""
import sys, os, struct, hashlib, gzip, io, stat

root, stock_boot, out_img = sys.argv[1], sys.argv[2], sys.argv[3]
PART_SIZE = 0x2000000          # recovery partition (32 MB)
RAMDISK_ADDR = 0x84000000
CMDLINE = (b'rdinit=/init androidboot.hardware=qcom ehci-hcd.park=3 msm_rtb.filter=0x37 '
           b'lpm_levels.sleep_disabled=1 androidboot.bootdevice=7824900.sdhci androidboot.selinux=permissive')

ino = [400000]
def ent(name, mode, data=b'', rdev=(0, 0), nlink=1):
    ino[0] += 1
    nb = name.encode() + b'\0'
    h = b'070701' + b''.join(b'%08X' % v for v in (
        ino[0], mode, 0, 0, nlink, 0, len(data), 0, 0, rdev[0], rdev[1], len(nb), 0))
    o = h + nb
    o += b'\0' * ((4 - len(o) % 4) % 4)
    o += data
    o += b'\0' * ((4 - len(o) % 4) % 4)
    return o

parts = []
dirs = {'dev', 'proc', 'sys', 'tmp', 'run', 'data', 'root', 'mnt', 'mnt/sd', 'usr', 'usr/bin', 'usr/sbin'}
for dp, dn, fn in os.walk(root):
    rel = os.path.relpath(dp, root).replace('\\', '/')
    if rel != '.':
        dirs.add(rel)
for d in sorted(dirs, key=lambda p: (p.count('/'), p)):
    parts.append(ent(d, stat.S_IFDIR | 0o755, nlink=2))
parts.append(ent('dev/console', stat.S_IFCHR | 0o600, rdev=(5, 1)))
parts.append(ent('dev/null', stat.S_IFCHR | 0o666, rdev=(1, 3)))
count = 0
for dp, dn, fn in os.walk(root):
    rel = os.path.relpath(dp, root).replace('\\', '/')
    for f in sorted(fn):
        path = f if rel == '.' else rel + '/' + f
        exe = path == 'init' or path.split('/')[0] in ('bin', 'sbin', 'lib')
        mode = stat.S_IFREG | (0o755 if exe else 0o644)
        parts.append(ent(path, mode, open(os.path.join(dp, f), 'rb').read()))
        count += 1
parts.append(ent('bin/sh', stat.S_IFLNK | 0o777, b'busybox'))
parts.append(ent('TRAILER!!!', 0))
cpio = b''.join(parts)
cpio += b'\0' * ((512 - len(cpio) % 512) % 512)
buf = io.BytesIO()
with gzip.GzipFile(fileobj=buf, mode='wb', compresslevel=9, mtime=0) as g:
    g.write(cpio)
ramdisk = buf.getvalue()

stock = open(stock_boot, 'rb').read()
ks, ka, rs, ra, ss, sa, ta, ps = struct.unpack('<8I', stock[8:40])
kernel = stock[ps:ps + ks]

def sha_id(*blobs):
    h = hashlib.sha1()
    for b in blobs:
        h.update(b); h.update(struct.pack('<I', len(b)))
    return h.digest()
assert sha_id(kernel, b'', b'') == stock[0x240:0x254]

hdr = bytearray(ps)
struct.pack_into('<8s8I', hdr, 0, b'ANDROID!', len(kernel), ka, len(ramdisk), RAMDISK_ADDR, 0, sa, ta, ps)
hdr[48:48 + 11] = b'tasrecovery'
hdr[64:64 + len(CMDLINE)] = CMDLINE
hdr[0x240:0x254] = sha_id(kernel, ramdisk, b'')
pad = lambda b: b + b'\0' * ((ps - len(b) % ps) % ps)
img = bytes(hdr) + pad(kernel) + pad(ramdisk)
open(out_img, 'wb').write(img)
open(os.path.splitext(out_img)[0] + '.cpio.gz', 'wb').write(ramdisk)
print('files %d  cpio %d  ramdisk.gz %d  image %d  (recovery partition %d, free %d)' % (
    count, len(cpio), len(ramdisk), len(img), PART_SIZE, PART_SIZE - len(img)))
assert len(img) <= PART_SIZE, 'image does not fit the recovery partition'
