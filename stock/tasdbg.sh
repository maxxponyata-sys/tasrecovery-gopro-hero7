#!/bin/sh
# tasdbg.sh - run by "tasdbg watch" on a triple press of the side button (stock GoPro firmware).
# The GoPro UI holds every MDSS display pipe, so the screen can only be taken over with gpcam and
# qmmf-server stopped - and they do not recover their display afterwards, so we reboot back.
logger -t tasdbg "debug screen requested"
# procmon would reboot the camera ~10 s after gpcam disappears - stop it first
for p in $(ps -o pid,args | grep '[e]tc/procmon' | awk '{print $1}'); do kill $p; done
for n in gpcam qmmf-server; do
    p=$(pidof $n); [ -n "$p" ] || continue
    kill $p
    i=0; while [ -n "$(pidof $n)" ] && [ $i -lt 10 ]; do sleep 0.3; i=$((i + 1)); done
    [ -n "$(pidof $n)" ] && kill -9 $(pidof $n)
done
sleep 0.5
/usr/bin/tasdbg ui
logger -t tasdbg "debug screen closed, rebooting"
sync
reboot
