# ADB patch for GoPro HERO7 Silver/White firmware updates

`patch_update_adb.py` takes the firmware **UPDATE.zip** of a HERO7 Silver/White ("Tasmania", APQ8053) and
produces a patched package that you install the normal way (UPDATE folder on the SD card). After the update
the camera exposes a **root adb shell over USB** (`18D1:4EE7`) instead of MTP.

```
python3 patch_update_adb.py UPDATE.zip -o patched
# -> patched/UPDATE/      copy this folder to the root of the SD card, then power the camera on
# -> patched/UPDATE.zip   the same, zipped
adb devices
adb shell
```
Needs only Python 3 (standard library). Runs in ~30 s.

## What it changes
Nothing but these files - all patched **in place** inside the blocks they already own in the ext4 system image,
so `system.transfer.list` stays valid and every other entry of `CAMFW.zip` (boot, modem, recovery, bootloaders,
updater script, ...) is copied byte for byte:

| file | change |
|---|---|
| `/usr/bin/start_mtp.sh` | gpcam runs it when it wants MTP; now it switches USB to adb-only (FunctionFS) and keeps it there |
| `/etc/rc5.d/S99GoPro.sh` | starts `start_mtp.sh adbwatch` at boot: re-asserts adb every 5 s (accepts `adb` or `ffs` as the active function) |
| `/etc/rc5.d/S00GoPro.sh` | creates `/tmp/class/android_usb/android0` before gpcam starts |
| `/usr/bin/gpcam` | its 9 `/sys/class/android_usb/android0/` strings become `/tmp/class/...` (same length), so gpcam can no longer flip USB back to MTP - that made adb drop and procmon reboot the camera |
| `updater-script` | `range_sha1()` checks of the system partition (if any) are recomputed |
| `CAMVER.bin` | CRC32 of the new `CAMFW.zip` (offset 0x24); everything else unchanged |

`--no-gpcam-fix` skips the S00/gpcam part (adb then works, but gpcam may re-enable MTP when a cable is plugged in).

The script refuses incremental packages, already patched packages, ext4 with `metadata_csum`, and packages
whose `CAMVER.bin` CRC does not match.

## Tested
- Firmware H18.03.02.10.00 package (HERO7 Silver, camera ids 29/32/33): 8 system blocks change - exactly the set
  of the hand-made image that has been running on a camera with stable adb; e2fsck clean (apart from a
  pre-existing padding warning in the stock image).
- Other firmware versions should work as long as the files above exist; check the script output.

## Notes
- Installing a firmware update always carries some risk. Keep a backup (TasRecovery `tas-backup`, or
  `dd` the partitions over adb once you have it).
- No GoPro code is included here - the script edits the package you already own.
- To go back, install the unmodified UPDATE.zip.
