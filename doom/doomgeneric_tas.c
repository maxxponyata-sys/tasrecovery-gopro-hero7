/* doomgeneric platform layer for the GoPro HERO7 (TasRecovery).
 *
 * Display: Doom renders 320x200; it is scaled 1.5x to 480x300 and rotated onto the 320x480 portrait
 * MDSS panel, which sits in landscape in the camera. Framebuffer sequence as in the stock GoPro
 * recovery (PUT_VSCREENINFO NOW once, then VBL|FORCE per frame), WLED backlight on after frame 1.
 *
 * Controls (landscape coordinates, 480x320):
 *   touch, left half ........ floating joystick (walk / turn)
 *   touch, right half top ... use / Enter          touch, right half bottom ... fire
 *   touch, top-left corner .. Esc (menu)
 *   top (shutter) button .... fire; hold 3 s = quit back to TasRecovery
 *   side (mode) button ...... use + Enter + 'y'; hold 1.5 s = rotate the picture by 180 degrees
 *
 * Environment: TAS_ROT=90|270 (default 90).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomkeys.h"
#include "doomgeneric.h"
#include "lsys.h"

#define PW 320          /* panel */
#define PH 480
#define LW 480          /* landscape view */
#define LH 320
#define KEY_SIDE 744
#define KEY_TOP  745

static int fbfd = -1, rot = 90, lit;
static unsigned char *fbmem;
static unsigned int fbline = PW * 4, var[40], fix[17];
static unsigned int back[PW * PH];
static struct { int fd; int touch; } dev[8];
static int ndev;
static int tminx, tmaxx = PW, tminy, tmaxy = PH;

/* ---------------------------------------------------------------- key queue */
static unsigned short kq[64];
static unsigned int kqr, kqw;
static void key(int pressed, unsigned char k) { kq[kqw++ % 64] = (unsigned short)((pressed << 8) | k); }

int DG_GetKey(int *pressed, unsigned char *k)
{
    if (kqr == kqw) return 0;
    unsigned short v = kq[kqr++ % 64];
    *pressed = v >> 8; *k = v & 0xff;
    return 1;
}

/* ---------------------------------------------------------------- display */
static void fb_init(void)
{
    const char *path = getenv("TAS_FB");     /* a plain file works too (host testing under qemu) */
    fbfd = (int)LS3(LNR_open, path ? path : "/dev/fb0", L_O_RDWR, 0);
    if (fbfd < 0) { printf("doom: cannot open /dev/fb0\n"); exit(1); }
    LS3(LNR_ioctl, fbfd, 0x4611, 0);                     /* unblank */
    LS3(LNR_ioctl, fbfd, 0x4602, fix);
    LS3(LNR_ioctl, fbfd, 0x4600, var);
    var[4] = var[5] = 0; var[21] = 0;                    /* activate NOW */
    LS3(LNR_ioctl, fbfd, 0x4601, var);
    LS3(LNR_ioctl, fbfd, 0x4602, fix);
    if (fix[11] >= PW * 4 && fix[11] <= PW * 16) fbline = fix[11];
    unsigned long len = fix[5] ? fix[5] : fbline * PH * 2;
    fbmem = (unsigned char *)lsys(LNR_mmap2, 0, (long)len, 3, 1, fbfd, 0);
    if ((unsigned long)fbmem >= 0xfffff000UL) { printf("doom: mmap failed\n"); exit(1); }
}

static void fb_commit(void)
{
    for (int y = 0; y < PH; y++) memcpy(fbmem + (unsigned long)y * fbline, back + y * PW, PW * 4);
    var[4] = var[5] = 0; var[21] = 16 | 128;            /* VBL | FORCE */
    LS3(LNR_ioctl, fbfd, 0x4601, var);
    var[21] = 16;
    LS3(LNR_ioctl, fbfd, 0x4606, var);
    if (!lit) {
        int b = (int)LS3(LNR_open, "/sys/class/leds/wled/brightness", L_O_WRONLY, 0);
        if (b >= 0) { LS3(LNR_write, b, "2048", 4); LS3(LNR_close, b, 0, 0); }
        lit = 1;
    }
}

/* landscape (lx,ly) -> panel index */
static inline int pidx(int lx, int ly)
{
    if (rot == 90) return lx * PW + (PW - 1 - ly);
    return (PH - 1 - lx) * PW + ly;
}
/* panel (px,py) -> landscape */
static void to_land(int px, int py, int *lx, int *ly)
{
    if (rot == 90) { *lx = py; *ly = PW - 1 - px; }
    else { *lx = PH - 1 - py; *ly = px; }
}

void DG_DrawFrame(void)
{
    static int sx[LW], sy[LH], ready;
    if (!ready) {
        for (int x = 0; x < LW; x++) sx[x] = x * 2 / 3;
        for (int y = 0; y < LH; y++) sy[y] = (y >= 10 && y < 310) ? (y - 10) * 2 / 3 : -1;
        ready = 1;
    }
    for (int ly = 0; ly < LH; ly++) {
        const pixel_t *src = sy[ly] >= 0 ? DG_ScreenBuffer + sy[ly] * DOOMGENERIC_RESX : 0;
        for (int lx = 0; lx < LW; lx++) {
            unsigned int c = 0xff000000u;
            if (src) {
                unsigned int p = src[sx[lx]];   /* 0x00RRGGBB -> panel RGBA (R in byte 0) */
                c = 0xff000000u | ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
            }
            back[pidx(lx, ly)] = c;
        }
    }
    fb_commit();
}

/* ---------------------------------------------------------------- input */
static void input_init(void)
{
    for (int i = 0; i < 8; i++) {
        char p[24]; snprintf(p, sizeof p, "/dev/input/event%d", i);
        int fd = (int)LS3(LNR_open, p, L_O_NONBLOCK, 0);
        if (fd < 0) continue;
        int ax[6] = { 0 }, ay[6] = { 0 };
        int t = LS3(LNR_ioctl, fd, 0x80184540 + 0x35, ax) == 0 && ax[2] > 0;
        if (t) { LS3(LNR_ioctl, fd, 0x80184540 + 0x36, ay); tminx = ax[1]; tmaxx = ax[2]; tminy = ay[1]; tmaxy = ay[2]; }
        dev[ndev].fd = fd; dev[ndev].touch = t; ndev++;
    }
}

/* touch state -> set of held keys */
static int touching, rawx, rawy, jx, jy, jset;
static unsigned char held[4];          /* currently pressed by touch */
static unsigned int side_down, top_down;

static void set_touch_keys(unsigned char *want)
{
    for (int i = 0; i < 4; i++) {                     /* release keys no longer wanted */
        if (!held[i]) continue;
        int keep = 0; for (int j = 0; j < 4; j++) if (want[j] == held[i]) keep = 1;
        if (!keep) key(0, held[i]);
    }
    for (int j = 0; j < 4; j++) {                     /* press new keys */
        if (!want[j]) continue;
        int had = 0; for (int i = 0; i < 4; i++) if (held[i] == want[j]) had = 1;
        if (!had) key(1, want[j]);
    }
    memcpy(held, want, 4);
}

static void touch_update(void)
{
    unsigned char want[4] = { 0, 0, 0, 0 };
    if (touching) {
        int px = tmaxx > tminx ? (rawx - tminx) * PW / (tmaxx - tminx + 1) : rawx;
        int py = tmaxy > tminy ? (rawy - tminy) * PH / (tmaxy - tminy + 1) : rawy;
        int lx, ly; to_land(px, py, &lx, &ly);
        if (lx < 70 && ly < 60) want[0] = KEY_ESCAPE;
        else if (lx < LW / 2) {                       /* floating joystick */
            if (!jset) { jx = lx; jy = ly; jset = 1; }
            int dx = lx - jx, dy = ly - jy;
            if (dy < -18) want[0] = KEY_UPARROW;
            if (dy > 18)  want[0] = KEY_DOWNARROW;
            if (dx < -18) want[1] = KEY_LEFTARROW;
            if (dx > 18)  want[1] = KEY_RIGHTARROW;
        } else if (ly < LH / 2) { want[0] = KEY_USE; want[1] = KEY_ENTER; }
        else want[0] = KEY_FIRE;
    } else jset = 0;
    set_touch_keys(want);
}

static void poll_input(void)
{
    struct { unsigned int s, us; unsigned short type, code; int value; } ev[32];
    for (int d = 0; d < ndev; d++) {
        long n;
        while ((n = LS3(LNR_read, dev[d].fd, ev, sizeof ev)) > 0) {
            for (int i = 0; i < n / 16; i++) {
                int t = ev[i].type, c = ev[i].code, v = ev[i].value;
                if (dev[d].touch) {
                    if (t == 3 && (c == 0x35 || c == 0)) rawx = v;
                    else if (t == 3 && (c == 0x36 || c == 1)) rawy = v;
                    else if (t == 1 && c == 330) touching = v != 0;
                    else if (t == 0) touch_update();
                } else if (t == 1 && c == KEY_TOP) {
                    if (v == 1) { top_down = lms(); key(1, KEY_FIRE); }
                    else if (v == 0) { key(0, KEY_FIRE); top_down = 0; }
                } else if (t == 1 && c == KEY_SIDE) {
                    if (v == 1) { side_down = lms(); key(1, KEY_USE); key(1, KEY_ENTER); key(1, 'y'); }
                    else if (v == 0) {
                        key(0, KEY_USE); key(0, KEY_ENTER); key(0, 'y');
                        if (side_down && lms() - side_down >= 1500) rot = rot == 90 ? 270 : 90;
                        side_down = 0;
                    }
                }
            }
        }
    }
    if (top_down && lms() - top_down >= 3000) { printf("doom: quit (shutter held)\n"); exit(0); }
}

/* ---------------------------------------------------------------- doomgeneric API */
void DG_Init(void)
{
    const char *r = getenv("TAS_ROT");
    if (r && atoi(r) == 270) rot = 270;
    fb_init();
    input_init();
    printf("doom: fb line %u, %d input devices, rotation %d\n", fbline, ndev, rot);
}
void DG_SleepMs(uint32_t ms)
{
    long ts[2] = { (long)(ms / 1000), (long)(ms % 1000) * 1000000L };
    LS3(LNR_nanosleep, ts, 0, 0);
}
uint32_t DG_GetTicksMs(void) { poll_input(); return lms(); }
void DG_SetWindowTitle(const char *title) { (void)title; }

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    for (;;) doomgeneric_Tick();
    return 0;
}
