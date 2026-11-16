/* reboot2 ARG - reboot with a reason string (LINUX_REBOOT_CMD_RESTART2).
 * On the HERO7 LK bootloader: "bootloader" -> fastboot, "recovery" -> recovery, "edl" -> Qualcomm EDL. */
#define TAS_MAIN
#include "sys.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        tas_puts(2, "usage: reboot2 bootloader|recovery|edl|<reason>\n");
        return 1;
    }
    sys_sync();
    long r = sys6(__NR_reboot, (long)0xfee1dead, 672274793L, (long)0xa1b2c3d4, (long)argv[1], 0, 0);
    tas_puts(2, "reboot2: failed ");
    tas_putnum(2, r);
    tas_puts(2, "\n");
    return 1;
}
