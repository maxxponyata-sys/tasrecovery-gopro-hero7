#!/bin/sh
# TasRecovery sample full-screen app: owns the display while it runs (tasui hands it over).
# Draws 8 colour bars (320x480 RGBA) with awk and shows them for 5 seconds via tasfb.
awk 'BEGIN { split("255 255 255,255 255 0,0 255 255,0 255 0,255 0 255,255 0 0,0 0 255,0 0 0", c, ",");
  for (y = 0; y < 480; y++) for (b = 1; b <= 8; b++) { split(c[b], v, " ");
    for (x = 0; x < 40; x++) printf "%c%c%c%c", v[1], v[2], v[3], 255 } }' > /tmp/bars.raw
(cat /tmp/bars.raw; sleep 5) | tasfb
