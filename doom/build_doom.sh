#!/bin/sh
# Build Doom (doomgeneric, GPL-2.0) for the GoPro HERO7 with arm-none-eabi-gcc + newlib.
# Output: apps/doom/doom  (static ARM Linux executable). The WAD is NOT included - bring your own.
set -e
cd "$(dirname "$0")"
DG_REPO=https://github.com/ozkl/doomgeneric.git
DG_REV=e257a09527062bfb986b6e5e0780839b6cbd5b71
SRC=${DOOMGENERIC_SRC:-build/doomgeneric}
if [ ! -d "$SRC" ]; then
    mkdir -p build
    git clone "$DG_REPO" "$SRC"
    (cd "$SRC" && git checkout -q "$DG_REV")
fi
D="$SRC/doomgeneric"
CC=${CC:-arm-none-eabi-gcc}
CFLAGS="-marm -march=armv7-a -mfloat-abi=soft -O2 -DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE \
        -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -ffunction-sections -fdata-sections \
        -fno-stack-protector -w -I$D -I."
OBJS="dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main d_mode d_net f_finale f_wipe
      g_game hu_lib hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio m_argv m_bbox
      m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors p_enemy p_floor p_inter p_lights
      p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp
      r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff s_sound tables
      v_video wi_stuff w_checksum w_file w_main w_wad z_zone w_file_stdc i_input i_video doomgeneric"
mkdir -p build/obj
LIST=""
for o in $OBJS; do
    $CC $CFLAGS -c "$D/$o.c" -o "build/obj/$o.o"
    LIST="$LIST build/obj/$o.o"
done
$CC $CFLAGS -c doomgeneric_tas.c -o build/obj/doomgeneric_tas.o
$CC $CFLAGS -c newlib_linux.c -o build/obj/newlib_linux.o
mkdir -p ../apps/doom
$CC $CFLAGS -nostartfiles -static -Wl,-e,_start -Wl,--gc-sections -Wl,-Ttext=0x10000 -Wl,-z,max-page-size=4096 \
    -Wl,--build-id=none -o ../apps/doom/doom $LIST build/obj/doomgeneric_tas.o build/obj/newlib_linux.o \
    -specs=nosys.specs -lm -lc -lgcc
arm-none-eabi-strip ../apps/doom/doom
echo "built apps/doom/doom ($(wc -c < ../apps/doom/doom) bytes)"
