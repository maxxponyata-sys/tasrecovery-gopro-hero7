#!/bin/sh
# TasRecovery sample app: prints system information (shown in the viewer afterwards)
echo "== kernel";  uname -a
echo "== uptime";  cat /proc/uptime
echo "== battery"; cat /sys/class/power_supply/battery/capacity /sys/class/power_supply/battery/status 2>/dev/null
echo "== memory";  free
echo "== mounts";  df -h 2>/dev/null || df
echo "== cpu";     grep -E 'Hardware|processor' /proc/cpuinfo | sort -u
echo "== temps";   for z in /sys/class/thermal/thermal_zone*; do echo "$(cat $z/type 2>/dev/null) $(cat $z/temp 2>/dev/null)"; done | head -12
