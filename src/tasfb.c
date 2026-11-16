/* tasfb - frame server for the GoPro HERO7 (MDSS) framebuffer.
 *
 * Usage: tasfb [-v] [fbdev] < frames
 *   Reads raw frames (xres*yres*4 bytes, RGBA, tightly packed) from stdin and shows each one.
 *   Keeps the device open: MDSS powers the panel down when the last fd is closed.
 *
 * Display sequence copied from the stock GoPro recovery (ScreenRecoveryUI in /usr/bin/recovery):
 *   GET_FSCREENINFO, GET_VSCREENINFO, x/yoffset=0 activate=NOW, PUT_VSCREENINFO   (mode set, panel on)
 *   mmap, copy rows, x/yoffset=0 activate=VBL, PUT_VSCREENINFO                      (show frame)
 * The stock code never uses FBIOPAN_DISPLAY.
 */
#define TAS_MAIN
#include "sys.h"

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPAN_DISPLAY     0x4606
#define FBIOBLANK           0x4611
#define FB_ACTIVATE_FORCE   128
#define FB_BLANK_UNBLANK    0
#define FB_ACTIVATE_NOW     0
#define FB_ACTIVATE_VBL     16
#define PROT_READ  1
#define PROT_WRITE 2
#define MAP_SHARED 1

struct fb_bitfield { u32 offset, length, msb_right; };
struct fb_var_screeninfo {
    u32 xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale;
    struct fb_bitfield red, green, blue, transp;
    u32 nonstd, activate, height, width, accel_flags;
    u32 pixclock, left_margin, right_margin, upper_margin, lower_margin, hsync_len, vsync_len;
    u32 sync, vmode, rotate, colorspace, reserved[4];
};
struct fb_fix_screeninfo {
    char id[16];
    u32 smem_start, smem_len, type, type_aux, visual;
    u16 xpanstep, ypanstep, ywrapstep;
    u32 line_length, mmio_start, mmio_len, accel;
    u16 capabilities, reserved[2];
};

static int verbose;
static void logv(const char *what, long v)
{
    if (!verbose) return;
    tas_puts(2, "tasfb: "); tas_puts(2, what); tas_puts(2, "="); tas_putnum(2, v); tas_puts(2, "\n");
}

static u8 row[4096];

static int read_full(void *buf, size_t n)
{
    u8 *p = buf;
    while (n) {
        long r = sys_read(0, p, n);
        if (r <= 0) return -1;
        p += r; n -= r;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *dev = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'v') verbose = 1;
        else dev = argv[i];
    }
    long fd = dev ? sys_open(dev, O_RDWR) : sys_open("/dev/graphics/fb0", O_RDWR);
    if (fd < 0 && !dev) fd = sys_open("/dev/fb0", O_RDWR);
    logv("open", fd);
    if (fd < 0) return 1;

    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    memset(&fix, 0, sizeof fix);
    memset(&var, 0, sizeof var);

    logv("unblank", sys_ioctl(fd, FBIOBLANK, (void *)FB_BLANK_UNBLANK));
    logv("get_fix", sys_ioctl(fd, FBIOGET_FSCREENINFO, &fix));
    logv("get_var", sys_ioctl(fd, FBIOGET_VSCREENINFO, &var));
    var.xoffset = 0; var.yoffset = 0; var.activate = FB_ACTIVATE_NOW;
    logv("put_var_now", sys_ioctl(fd, FBIOPUT_VSCREENINFO, &var));
    sys_ioctl(fd, FBIOGET_FSCREENINFO, &fix);       /* smem_len/line_length may change after set_par */

    u32 xres = var.xres ? var.xres : 320, yres = var.yres ? var.yres : 480;
    u32 bpp = var.bits_per_pixel ? var.bits_per_pixel / 8 : 4;
    u32 line = fix.line_length ? fix.line_length : xres * bpp;
    u32 vres = var.yres_virtual > yres ? var.yres_virtual : yres;
    size_t maplen = fix.smem_len ? fix.smem_len : (size_t)line * vres;
    u32 inrow = xres * 4;
    logv("xres", xres); logv("yres", yres); logv("line", line); logv("smem_len", fix.smem_len);

    u8 *fb = sys_mmap2(0, maplen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    logv("mmap", (long)fb);
    if ((unsigned long)fb >= 0xfffff000UL) return 2;
    if (inrow > sizeof row) return 3;

    int lit = 0;
    for (;;) {
        for (u32 y = 0; y < yres; y++) {
            if (read_full(row, inrow) < 0) return 0;     /* EOF: exit, fd closes */
            memcpy(fb + (size_t)y * line, row, inrow);
        }
        /* fb_set_var() is a no-op when the var is unchanged, so FORCE a set_par on every frame
         * (needed to replace the bootloader's continuous splash), then pan to the same buffer. */
        var.xoffset = 0; var.yoffset = 0; var.activate = FB_ACTIVATE_VBL | FB_ACTIVATE_FORCE;
        long r = sys_ioctl(fd, FBIOPUT_VSCREENINFO, &var);
        logv("put_var_force", r);
        var.activate = FB_ACTIVATE_VBL;
        logv("pan", sys_ioctl(fd, FBIOPAN_DISPLAY, &var));
        if (!lit) {
            /* The panel is lit by the PMI8950 WLED; MDSS resets it to 0 whenever the panel
             * powers on, so turn it on once the first frame is up. */
            long b = sys_open("/sys/class/leds/wled/brightness", O_RDWR);
            if (b >= 0) { sys_write(b, "2048", 4); sys_close(b); }
            logv("wled", b);
            lit = 1;
        }
    }
}
