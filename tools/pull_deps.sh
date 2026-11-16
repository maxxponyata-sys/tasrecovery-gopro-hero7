#!/bin/bash
# Extract adbd, leprop-service, getprop, setprop and every library they need (resolving symlinks)
# from the stock GoPro system image into the TasRecovery rootfs.
#
# Usage: tools/pull_deps.sh path/to/system.img [rootfs]
#   system.img: the raw ext4 system image (e.g. from CAMFW.zip: system.new.dat + system.transfer.list
#   converted with sdat2img). Needs debugfs (e2fsprogs) and readelf (binutils).
set -e
I=${1:?usage: $0 system.img [rootfs]}
OUT=${2:-$(dirname "$0")/../rootfs}
mkdir -p "$OUT/lib" "$OUT/sbin"

resolve() { # print the final non-symlink path inside the image
  local p=$1 n=0
  while [ $n -lt 10 ]; do
    local st; st=$(debugfs -R "stat $p" "$I" 2>/dev/null)
    [ -z "$st" ] && { echo ""; return; }
    if echo "$st" | grep -q 'Type: symlink'; then
      local t; t=$(echo "$st" | sed -n 's/^Fast link dest: "\(.*\)"/\1/p')
      [ -z "$t" ] && t=$(debugfs -R "cat $p" "$I" 2>/dev/null)
      case $t in /*) p=$t ;; *) p=$(dirname "$p")/$t ;; esac
      n=$((n+1))
    else echo "$p"; return; fi
  done
}

declare -A seen
queue=(/sbin/adbd /sbin/leprop-service /usr/bin/getprop /usr/bin/setprop /lib/ld-linux.so.3)
while [ ${#queue[@]} -gt 0 ]; do
  f=${queue[0]}; queue=("${queue[@]:1}")
  [ -n "${seen[$f]}" ] && continue; seen[$f]=1
  real=$(resolve "$f"); [ -z "$real" ] && { echo "MISSING $f"; continue; }
  case $f in /sbin/*|/usr/bin/*) dst=$OUT/sbin/$(basename "$f") ;; *) dst=$OUT/lib/$(basename "$f") ;; esac
  debugfs -R "dump $real $dst" "$I" 2>/dev/null; chmod 755 "$dst"
  echo "$f -> $real ($(stat -c %s "$dst") bytes)"
  for n in $(readelf -d "$dst" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
    for dir in /lib /usr/lib; do
      [ -n "$(debugfs -R "stat $dir/$n" "$I" 2>/dev/null)" ] && { queue+=("$dir/$n"); break; }
    done
  done
done
echo "interpreter: $(readelf -l "$OUT/sbin/adbd" | sed -n 's/.*interpreter: \(.*\)]/\1/p')"
du -sh "$OUT/lib" "$OUT/sbin"
