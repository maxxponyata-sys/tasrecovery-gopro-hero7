#!/bin/sh
# Build the TasRecovery helper binaries (static, no libc) and the recovery image.
# Needs: arm-none-eabi-gcc, python3. Stock GoPro boot.img path as $1 (kernel source for the image).
set -e
cd "$(dirname "$0")"
CC=${CC:-arm-none-eabi-gcc}
CFLAGS="-marm -march=armv7-a -mfloat-abi=soft -Os -nostdlib -static -ffreestanding -fno-builtin \
        -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -Wno-unused-parameter"
LDFLAGS="-Wl,-e,_start -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-Ttext=0x10000 -Wl,--gc-sections -Wl,-z,max-page-size=4096"
for p in tasui tasfb tasbtn tasdbg reboot2 fbinfo; do
    $CC $CFLAGS $LDFLAGS -o rootfs/sbin/$p src/$p.c -lgcc
    arm-none-eabi-strip rootfs/sbin/$p 2>/dev/null || true
    echo "built rootfs/sbin/$p ($(wc -c < rootfs/sbin/$p) bytes)"
done
if [ -n "$1" ]; then
    python3 tools/mkrecovery.py rootfs "$1" tasrecovery.img
fi
