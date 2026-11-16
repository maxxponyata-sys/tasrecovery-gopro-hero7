/* fbinfo [fbdev] - print the framebuffer geometry and pixel layout (debug helper). */
#define TAS_MAIN
#include "sys.h"

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

static void kv(const char *k, long v) { tas_puts(1, k); tas_puts(1, "="); tas_putnum(1, v); tas_puts(1, " "); }

int main(int argc, char **argv)
{
    long fd = sys_open(argc > 1 ? argv[1] : "/dev/fb0", O_RDONLY);
    if (fd < 0) { tas_puts(2, "fbinfo: open failed\n"); return 1; }
    u32 var[40], fix[17];
    memset(var, 0, sizeof var); memset(fix, 0, sizeof fix);
    long a = sys_ioctl(fd, FBIOGET_VSCREENINFO, var);
    long b = sys_ioctl(fd, FBIOGET_FSCREENINFO, fix);
    kv("get_var", a); kv("get_fix", b); tas_puts(1, "\n");
    kv("xres", var[0]); kv("yres", var[1]); kv("xres_virtual", var[2]); kv("yres_virtual", var[3]);
    kv("bpp", var[6]); tas_puts(1, "\n");
    kv("red", var[8]); kv("green", var[11]); kv("blue", var[14]); kv("transp", var[17]); tas_puts(1, "\n");
    kv("smem_len", fix[5]); kv("line_length", fix[11]);   /* byte offset 44 */
    tas_puts(1, "\n");
    return 0;
}
