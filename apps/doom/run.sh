#!/bin/sh
# DOOM for TasRecovery (doomgeneric). Put your own IWAD next to this script (doom.wad, doom2.wad,
# doom1.wad, ...). Saves and config go to this folder. Touch: left half walks, right half top = use,
# bottom = fire, top-left corner = menu. Shutter = fire (hold 3 s to quit), side button = use/enter.
cd "$TAS_APPDIR" 2>/dev/null || cd "$(dirname "$0")"
WAD=$(ls doom.wad DOOM.WAD doom2.wad DOOM2.WAD doom1.wad DOOM1.WAD plutonia.wad tnt.wad freedoom1.wad freedoom2.wad 2>/dev/null | head -1)
[ -n "$WAD" ] || { echo "no WAD found in $(pwd)"; exit 1; }
[ -f rotation ] && export TAS_ROT=$(cat rotation)
export HOME="$(pwd)"
chmod +x ./doom
exec ./doom -iwad "$WAD"
