#!/bin/sh
# TasRecovery sample app: toggles the front/back status LEDs at full brightness
F=/sys/class/leds/front_led/brightness
B=/sys/class/leds/back_led/brightness
if [ "$(cat $F 2>/dev/null)" = "0" ]; then V=255; else V=0; fi
echo $V > $F; echo $V > $B
echo "LEDs: $V"
