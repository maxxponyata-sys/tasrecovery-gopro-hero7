/* tasui - TasRecovery user interface for the GoPro HERO7 (320x480 MDSS panel, ft5x06 touch, PMIC keys).
 *
 * Home screen, file explorer (recovery root, eMMC partitions, SD card) and app manager.
 * Touch: tap = open/select, drag = scroll, tap the title bar = back.
 * Buttons (qpnp_pon 744/745): short press = next item, hold >= 0.7 s = select.
 *
 * Usage: tasui [--fb PATH] [--shot DIR]     (--shot renders the main screens as DIR/shotN.raw and exits)
 */
#define TAS_MAIN
#include "sys.h"
#include "font24.h"
#include "font16.h"

/* ------------------------------------------------------------------ small libc */
static void scpy(char *d, const char *s, size_t n) { size_t i = 0; if (!n) return; for (; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = 0; }
static void scat(char *d, const char *s, size_t n) { size_t l = tas_strlen(d); if (l < n) scpy(d + l, s, n - l); }
static int scmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (u8)*a - (u8)*b; }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int scasecmp(const char *a, const char *b) { while (*a && lower(*a) == lower(*b)) a++, b++; return lower((u8)*a) - lower((u8)*b); }
static int sstarts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static const char *sbase(const char *p) { const char *b = p; for (; *p; p++) if (*p == '/' && p[1]) b = p + 1; return b; }
static void utoa(unsigned long long v, char *b) { char t[24]; int i = 0; do { t[i++] = '0' + v % 10; v /= 10; } while (v); int j = 0; while (i) b[j++] = t[--i]; b[j] = 0; }
static void fmt_size(long long v, char *b)
{
    const char *u[] = { "B", "KB", "MB", "GB" }; int i = 0; long long w = v * 10;
    while (w >= 10240 && i < 3) { w /= 1024; i++; }
    char t[24]; utoa(w / 10, t); scpy(b, t, 24);
    if (i) { char d[4] = { '.', (char)('0' + w % 10), 0 }; scat(b, d, 24); }
    scat(b, " ", 24); scat(b, u[i], 24);
}
static void path_join(char *out, const char *dir, const char *name, size_t n)
{
    scpy(out, dir, n);
    if (!(dir[0] == '/' && dir[1] == 0)) scat(out, "/", n);
    scat(out, name, n);
}
static void path_parent(char *p)
{
    size_t l = tas_strlen(p);
    while (l > 1 && p[l - 1] == '/') p[--l] = 0;
    while (l > 1 && p[l - 1] != '/') p[--l] = 0;
    if (l > 1) p[l - 1] = 0;
}
/* single-quote a string for /bin/sh */
static void shq(char *out, const char *s, size_t n)
{
    size_t o = 0;
    if (o + 1 < n) out[o++] = '\'';
    for (; *s && o + 5 < n; s++) {
        if (*s == '\'') { out[o++] = '\''; out[o++] = '\\'; out[o++] = '\''; out[o++] = '\''; }
        else out[o++] = *s;
    }
    if (o + 1 < n) out[o++] = '\'';
    out[o] = 0;
}

static int logfd = -1;
static void logs(const char *a, const char *b)
{
    if (logfd < 0) return;
    tas_puts(logfd, "tasui: "); tas_puts(logfd, a); if (b) tas_puts(logfd, b); tas_puts(logfd, "\n");
}

/* ------------------------------------------------------------------ framebuffer */
#define W 320
#define H 480
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPAN_DISPLAY     0x4606
#define FBIOBLANK           0x4611
#define FB_ACTIVATE_VBL     16
#define FB_ACTIVATE_FORCE   128

static u32 back[W * H];
static u8 *fbmem; static int fbfd = -1; static u32 fbline = W * 4; static int lit;
static u32 fbvar[40]; static u32 fbfix[17];
static const char *fbpath = "/dev/fb0";
static const char *shotdir;
static int shotn;

#define RGB(r, g, b) (0xff000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))
#define C_BG     RGB(12, 14, 18)
#define C_BAR    RGB(24, 28, 36)
#define C_FG     RGB(232, 234, 239)
#define C_DIM    RGB(130, 138, 150)
#define C_ACC    RGB(26, 161, 232)
#define C_SEL    RGB(18, 70, 110)
#define C_LINE   RGB(36, 40, 50)
#define C_RED    RGB(229, 72, 77)
#define C_GRN    RGB(60, 200, 110)
#define C_YEL    RGB(240, 190, 60)

static int fb_open(void)
{
    fbfd = sys_open(fbpath, O_RDWR);
    if (fbfd < 0) { logs("fb open failed", 0); return -1; }
    memset(fbvar, 0, sizeof fbvar); memset(fbfix, 0, sizeof fbfix);
    sys_ioctl(fbfd, FBIOBLANK, 0);
    sys_ioctl(fbfd, FBIOGET_FSCREENINFO, fbfix);
    sys_ioctl(fbfd, FBIOGET_VSCREENINFO, fbvar);
    fbvar[4] = fbvar[5] = 0; fbvar[21] = 0;                    /* x/yoffset = 0, activate = NOW */
    sys_ioctl(fbfd, FBIOPUT_VSCREENINFO, fbvar);
    sys_ioctl(fbfd, FBIOGET_FSCREENINFO, fbfix);
    u32 line = fbfix[11];          /* fb_fix_screeninfo.line_length is at byte offset 44 */
    if (line >= W * 4 && line <= W * 16) fbline = line;
    size_t len = fbfix[5] ? fbfix[5] : (size_t)fbline * H * 2;
    if (len < (size_t)fbline * H) len = (size_t)fbline * H;
    fbmem = sys_mmap2(0, len, 3, 1, fbfd, 0);
    if ((unsigned long)fbmem >= 0xfffff000UL) { fbmem = 0; logs("fb mmap failed", 0); return -1; }
    lit = 0;
    return 0;
}
static void fb_close(void)
{
    if (fbfd >= 0) sys_close(fbfd);   /* mapping stays until exec/exit; MDSS powers the panel down */
    fbfd = -1; fbmem = 0;
}
static void flush(void)
{
    if (shotdir) {
        char p[256], n[16]; utoa(shotn++, n);
        path_join(p, shotdir, "shot", sizeof p); scat(p, n, sizeof p); scat(p, ".raw", sizeof p);
        long f = sys_open(p, O_WRONLY | O_CREAT | O_TRUNC);
        if (f >= 0) { sys_write(f, back, sizeof back); sys_close(f); }
        return;
    }
    if (!fbmem) return;
    for (int y = 0; y < H; y++) memcpy(fbmem + (size_t)y * fbline, back + y * W, W * 4);
    fbvar[4] = fbvar[5] = 0; fbvar[21] = FB_ACTIVATE_VBL | FB_ACTIVATE_FORCE;
    sys_ioctl(fbfd, FBIOPUT_VSCREENINFO, fbvar);
    fbvar[21] = FB_ACTIVATE_VBL;
    sys_ioctl(fbfd, FBIOPAN_DISPLAY, fbvar);
    if (!lit) {   /* PMI8950 WLED is the backlight; MDSS resets it to 0 at panel power-on */
        long b = sys_open("/sys/class/leds/wled/brightness", O_WRONLY);
        if (b >= 0) { sys_write(b, "2048", 4); sys_close(b); }
        lit = 1;
    }
}

static void fill(int x, int y, int w, int h, u32 c)
{
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    for (int j = 0; j < h; j++) { u32 *p = back + (y + j) * W + x; for (int i = 0; i < w; i++) p[i] = c; }
}
/* draws text, clipped to maxw pixels; returns end x */
static int text(int x, int y, const char *s, u32 c, int big, int maxw)
{
    int fw = big ? FONT24_W : FONT16_W, fh = big ? FONT24_H : FONT16_H, rb = big ? FONT24_ROWBYTES : FONT16_ROWBYTES;
    int lim = maxw > 0 ? x + maxw : W;
    for (; *s; s++) {
        if (x + fw > lim) {   /* ellipsis */
            if (x + fw <= W) for (int k = 0; k < 3 && x + 2 < W; k++) fill(x + 1 + k * 3, y + fh - 6, 2, 2, c);
            break;
        }
        int ch = (u8)*s; if (ch < 32 || ch > 126) ch = '?';
        const u8 *g = big ? font24_data[ch - 32] : font16_data[ch - 32];
        for (int r = 0; r < fh; r++) {
            int py = y + r; if (py < 0 || py >= H) continue;
            for (int b = 0; b < fw; b++) if (g[r * rb + b / 8] & (0x80 >> (b % 8))) {
                int px = x + b; if (px >= 0 && px < W) back[py * W + px] = c;
            }
        }
        x += fw;
    }
    return x;
}
static int text_w(const char *s, int big) { return (int)tas_strlen(s) * (big ? FONT24_W : FONT16_W); }

/* ------------------------------------------------------------------ input */
#define EV_SYN 0
#define EV_KEY 1
#define EV_ABS 3
#define BTN_TOUCH 330
#define ABS_X 0
#define ABS_Y 1
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36

struct pollfd { int fd; short events, revents; };
struct absinfo { int value, minimum, maximum, fuzz, flat, resolution; };
static struct pollfd pfd[8]; static int npfd; static int istouch[8];
static int tminx, tmaxx = W, tminy, tmaxy = H, tswap, tinvx, tinvy;

enum { EV_NONE, EV_TAP, EV_DRAG, EV_NEXT, EV_SELECT };
struct uev { int type, x, y, dy; };

static void input_open(void)
{
    npfd = 0;
    for (int i = 0; i < 8; i++) {
        char p[32] = "/dev/input/event"; char n[4]; utoa(i, n); scat(p, n, sizeof p);
        long fd = sys_open(p, O_RDONLY);
        if (fd < 0) continue;
        struct absinfo ax, ay; memset(&ax, 0, sizeof ax); memset(&ay, 0, sizeof ay);
        int t = sys_ioctl(fd, 0x80184540 + ABS_MT_POSITION_X, &ax) == 0 && ax.maximum > 0;
        if (t) {
            sys_ioctl(fd, 0x80184540 + ABS_MT_POSITION_Y, &ay);
            tminx = ax.minimum; tmaxx = ax.maximum; tminy = ay.minimum; tmaxy = ay.maximum;
        }
        pfd[npfd].fd = fd; pfd[npfd].events = 1; istouch[npfd] = t; npfd++;
    }
    /* optional /etc/touch.conf: any of "swap", "invx", "invy" */
    char buf[64]; long f = sys_open("/etc/touch.conf", O_RDONLY);
    if (f >= 0) {
        long n = sys_read(f, buf, sizeof buf - 1); sys_close(f); buf[n > 0 ? n : 0] = 0;
        for (char *s = buf; *s; s++) {
            if (sstarts(s, "swap")) tswap = 1;
            if (sstarts(s, "invx")) tinvx = 1;
            if (sstarts(s, "invy")) tinvy = 1;
        }
    }
}
static void input_close(void) { for (int i = 0; i < npfd; i++) sys_close(pfd[i].fd); npfd = 0; }

static void map_touch(int rx, int ry, int *sx, int *sy)
{
    int x = rx, y = ry, x0 = tminx, x1 = tmaxx, y0 = tminy, y1 = tmaxy;
    if (tswap) { int t = x; x = y; y = t; t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    *sx = x1 > x0 ? (x - x0) * W / (x1 - x0 + 1) : x;
    *sy = y1 > y0 ? (y - y0) * H / (y1 - y0 + 1) : y;
    if (tinvx) *sx = W - 1 - *sx;
    if (tinvy) *sy = H - 1 - *sy;
}

static int touching, tx0, ty0, tlast_y, rawx, rawy, tmoved, tfresh; static long long tdown;
static long long kdown;

/* blocks until a UI event */
static struct uev wait_event(void)
{
    struct uev e = { EV_NONE, 0, 0, 0 };
    if (shotdir || !npfd) { e.type = EV_NONE; return e; }
    for (;;) {
        for (int i = 0; i < npfd; i++) pfd[i].revents = 0;
        if (sys_poll(pfd, npfd, -1) <= 0) continue;
        for (int i = 0; i < npfd; i++) {
            if (!(pfd[i].revents & 1)) continue;
            struct { u32 s, us; u16 type, code; int value; } ev[16];
            long n = sys_read(pfd[i].fd, ev, sizeof ev);
            for (int k = 0; k < n / 16; k++) {
                int t = ev[k].type, c = ev[k].code, v = ev[k].value;
                if (istouch[i]) {
                    if (t == EV_ABS && (c == ABS_MT_POSITION_X || c == ABS_X)) rawx = v;
                    else if (t == EV_ABS && (c == ABS_MT_POSITION_Y || c == ABS_Y)) rawy = v;
                    else if (t == EV_KEY && c == BTN_TOUCH) {
                        if (v) { touching = 1; tfresh = 1; tmoved = 0; tdown = tas_ms(); }
                        else if (touching) {
                            touching = 0;
                            if (!tmoved && tas_ms() - tdown < 800) { e.type = EV_TAP; e.x = tx0; e.y = ty0; return e; }
                        }
                    } else if (t == EV_SYN && touching) {
                        int sx, sy; map_touch(rawx, rawy, &sx, &sy);
                        if (tfresh) { tx0 = sx; ty0 = sy; tlast_y = sy; tfresh = 0; }
                        else {
                            int ax = sx - tx0, ay = sy - ty0;
                            if (ax * ax + ay * ay > 18 * 18) tmoved = 1;
                            if (tmoved && sy != tlast_y) { e.type = EV_DRAG; e.dy = sy - tlast_y; tlast_y = sy; return e; }
                        }
                    }
                } else if (t == EV_KEY && c != BTN_TOUCH && (c < 320 || c > 335)) {
                    if (v == 1) kdown = tas_ms();
                    else if (v == 0 && kdown) {
                        long long d = tas_ms() - kdown; kdown = 0;
                        e.type = d >= 700 ? EV_SELECT : EV_NEXT; return e;
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ processes */
static char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", "HOME=/root", 0, 0, 0 };

/* runs /bin/sh -c cmd; output goes to logpath (or /tmp/tasui-cmd.log); returns exit status */
static int run_sh(const char *cmd, const char *logpath)
{
    logs("run: ", cmd);
    long pid = sys_fork();
    if (pid == 0) {
        long o = sys_open(logpath ? logpath : "/tmp/tasui-cmd.log", O_WRONLY | O_CREAT | O_APPEND);
        if (o >= 0) { sys_dup2(o, 1); sys_dup2(o, 2); }
        char *argv[] = { "sh", "-c", (char *)cmd, 0 };
        sys_execve("/bin/sh", argv, envp);
        sys_exit(127);
    }
    if (pid < 0) return -1;
    int st = 0; sys_wait4(pid, &st);
    return (st >> 8) & 0xff;
}

/* ------------------------------------------------------------------ widgets */
#define BAR_H 48
#define ROW_H 56
#define MAXI 1024

struct item { char label[96]; char sub[64]; char icon; u32 color; int tag; };
static struct item items[MAXI]; static int nitems;
static char bar_status[48];

static void bar(const char *title, int back)
{
    fill(0, 0, W, BAR_H, C_BAR);
    int x = 12;
    if (back) { text(10, 12, "<", C_ACC, 1, 0); x = 34; }
    int sw = bar_status[0] ? text_w(bar_status, 0) + 10 : 0;
    text(x, 12, title, C_FG, 1, W - x - 8 - sw);
    if (sw) text(W - sw + 2, 16, bar_status, C_DIM, 0, 0);
}
static void add(char icon, const char *label, const char *sub, u32 color, int tag)
{
    if (nitems >= MAXI) return;
    struct item *it = &items[nitems++];
    it->icon = icon; scpy(it->label, label, sizeof it->label); scpy(it->sub, sub ? sub : "", sizeof it->sub);
    it->color = color ? color : C_FG; it->tag = tag;
}
static const char *icon_str(char c)
{
    switch (c) {
    case 'd': return "[]"; case 'f': return "--"; case 'l': return "->"; case 'b': return "<<";
    case 'a': return "**"; case 'p': return "++"; case 'r': return "!!"; case 'm': return "::";
    case 'x': return "><"; default: return "  ";
    }
}
static void draw_list(const char *title, int back, int sel, int top)
{
    fill(0, 0, W, H, C_BG);
    bar(title, back);
    for (int i = top, y = BAR_H; i < nitems && y < H; i++, y += ROW_H) {
        struct item *it = &items[i];
        if (i == sel) fill(0, y, W, ROW_H, C_SEL);
        fill(0, y + ROW_H - 1, W, 1, C_LINE);
        int x = 8;
        if (it->icon) { text(x, y + 6, icon_str(it->icon), it->icon == 'd' ? C_YEL : C_DIM, 1, 0); x = 44; }
        text(x, y + 4, it->label, it->color, 1, W - x - 6);
        if (it->sub[0]) text(x, y + 32, it->sub, C_DIM, 0, W - x - 6);
    }
    if (nitems * ROW_H > H - BAR_H) {   /* scroll bar */
        int vis = (H - BAR_H) / ROW_H, th = (H - BAR_H) * vis / nitems; if (th < 20) th = 20;
        int ty = BAR_H + (H - BAR_H - th) * top / (nitems - vis > 0 ? nitems - vis : 1);
        fill(W - 3, ty, 3, th, C_DIM);
    }
    flush();
}
/* returns the chosen index, or -1 for back */
static int run_list(const char *title, int back, int sel)
{
    int vis = (H - BAR_H) / ROW_H, top = 0, acc = 0;
    if (sel < 0) sel = 0;
    if (sel >= nitems) sel = nitems - 1;
    if (sel >= vis) top = sel - vis + 1;
    for (;;) {
        draw_list(title, back, sel, top);
        if (shotdir) return -2;
        struct uev e = wait_event();
        if (e.type == EV_NEXT) {
            sel = (sel + 1) % nitems;
            if (sel < top) top = sel;
            if (sel >= top + vis) top = sel - vis + 1;
        } else if (e.type == EV_SELECT) {
            return sel;
        } else if (e.type == EV_TAP) {
            if (e.y < BAR_H) { if (back) return -1; continue; }
            int i = top + (e.y - BAR_H) / ROW_H;
            if (i < nitems) return i;
        } else if (e.type == EV_DRAG) {
            acc -= e.dy;
            while (acc >= ROW_H / 2) { if (top + vis < nitems) top++; acc -= ROW_H / 2; }
            while (acc <= -ROW_H / 2) { if (top > 0) top--; acc += ROW_H / 2; }
            if (sel < top) sel = top;
            if (sel >= top + vis) sel = top + vis - 1;
        }
    }
}
static void message(const char *title, const char *l1, const char *l2, u32 color, int wait)
{
    fill(0, 0, W, H, C_BG); bar(title, 0);
    text((W - text_w(l1, 1)) / 2, 180, l1, color, 1, W - 8);
    if (l2 && *l2) text((W - text_w(l2, 0)) / 2 > 4 ? (W - text_w(l2, 0)) / 2 : 4, 220, l2, C_DIM, 0, W - 8);
    if (wait) text((W - text_w("tap or press a button", 0)) / 2, H - 40, "tap or press a button", C_DIM, 0, 0);
    flush();
    if (wait && !shotdir) { struct uev e; do e = wait_event(); while (e.type == EV_DRAG); }
}
static void busy(const char *what) { message("Working", what, "do not power off", C_FG, 0); }
static int confirm(const char *title, const char *q)
{
    nitems = 0;
    add('x', "No, cancel", 0, 0, 0);
    add('r', "Yes", q, C_RED, 1);
    int r = run_list(title, 1, 0);
    return r == 1;
}

/* ------------------------------------------------------------------ text viewer */
static char vbuf[256 * 1024];
static void viewer(const char *path)
{
    long f = sys_open(path, O_RDONLY);
    if (f < 0) { message("View", "cannot open", sbase(path), C_RED, 1); return; }
    long n = sys_read(f, vbuf, sizeof vbuf - 1); sys_close(f);
    if (n < 0) n = 0;
    vbuf[n] = 0;
    int bin = 0; for (long i = 0; i < n && i < 4096; i++) if (!vbuf[i]) { bin = 1; break; }
    /* build line starts: text wraps at 40 cols, hex shows 8 bytes per line */
    static int starts[40000]; int nl = 0;
    if (bin) { for (long o = 0; o < n && nl < 40000; o += 8) starts[nl++] = o; }
    else {
        long i = 0;
        while (i <= n && nl < 40000) {
            starts[nl++] = i; int col = 0;
            while (i < n && vbuf[i] != '\n' && col < 39) { i++; col++; }
            if (i < n && vbuf[i] == '\n') i++;
            else if (i >= n) break;
        }
    }
    int rows = (H - BAR_H - 4) / FONT16_H, top = 0, acc = 0;
    for (;;) {
        fill(0, 0, W, H, C_BG); bar(sbase(path), 1);
        for (int r = 0; r < rows && top + r < nl; r++) {
            char line[48]; int y = BAR_H + 2 + r * FONT16_H; long s = starts[top + r];
            if (bin) {
                const char *hx = "0123456789abcdef"; int o = 0; unsigned long off = s;
                for (int k = 7; k >= 0; k--) line[o++] = hx[(off >> (k * 4)) & 15];
                line[o++] = ' ';
                for (int k = 0; k < 8 && s + k < n; k++) { u8 b = vbuf[s + k]; line[o++] = hx[b >> 4]; line[o++] = hx[b & 15]; line[o++] = ' '; }
                line[o] = 0;
            } else {
                int o = 0; long e = (top + r + 1 < nl) ? starts[top + r + 1] : n;
                for (long k = s; k < e && o < 40; k++) { char ch = vbuf[k]; if (ch == '\n') break; line[o++] = ch == '\t' ? ' ' : ch; }
                line[o] = 0;
            }
            text(2, y, line, C_FG, 0, W - 4);
        }
        if (n >= (long)sizeof vbuf - 1) text(W - 90, H - 18, "[256K max]", C_YEL, 0, 0);
        flush();
        if (shotdir) return;
        struct uev e = wait_event();
        if (e.type == EV_TAP && e.y < BAR_H) return;
        if (e.type == EV_SELECT) return;
        if (e.type == EV_NEXT || (e.type == EV_TAP && e.y > H / 2)) { top += rows - 2; if (top > nl - rows) top = nl - rows > 0 ? nl - rows : 0; }
        if (e.type == EV_TAP && e.y >= BAR_H && e.y <= H / 2) { top -= rows - 2; if (top < 0) top = 0; }
        if (e.type == EV_DRAG) {
            acc -= e.dy;
            while (acc >= FONT16_H) { if (top + rows < nl) top++; acc -= FONT16_H; }
            while (acc <= -FONT16_H) { if (top > 0) top--; acc += FONT16_H; }
        }
    }
}

/* ------------------------------------------------------------------ mounts */
static const char *emmc_parts[] = { "system", "userdata", "persist", "cache", "recoveryfs", "systemrw",
                                    "gpdata", "gpdebug", "gpupdate", "oem", 0 };
static int is_mounted(const char *mp)
{
    static char buf[16384];
    long f = sys_open("/proc/mounts", O_RDONLY); if (f < 0) return 0;
    long n = sys_read(f, buf, sizeof buf - 1); sys_close(f); if (n < 0) n = 0; buf[n] = 0;
    char pat[96] = " "; scat(pat, mp, sizeof pat); scat(pat, " ", sizeof pat);
    size_t pl = tas_strlen(pat);
    for (char *s = buf; *s; s++) { size_t i = 0; while (i < pl && s[i] == pat[i]) i++; if (i == pl) return 1; }
    return 0;
}
static int mount_part(const char *name, char *mp, size_t mpn)
{
    scpy(mp, "/mnt/emmc/", mpn); scat(mp, name, mpn);
    if (is_mounted(mp)) return 0;
    sys_mkdir("/mnt/emmc", 0755); sys_mkdir(mp, 0755);
    char dev[96] = "/dev/block/by-name/"; scat(dev, name, sizeof dev);
    if (sys_mount(dev, mp, "ext4", 0, 0) == 0) return 0;
    if (sys_mount(dev, mp, "vfat", 0, "utf8") == 0) return 0;
    return -1;
}

/* ------------------------------------------------------------------ explorer */
struct dent { char name[96]; u32 mode; long long size; };
static struct dent dents[MAXI]; static int ndents;
static int sorted[MAXI];
static char clip[512]; static int clipcut;

static int dent_less(int a, int b)
{
    int da = (dents[a].mode & 0170000) == 0040000, db = (dents[b].mode & 0170000) == 0040000;
    if (da != db) return da;
    return scasecmp(dents[a].name, dents[b].name) < 0;
}
static int read_dir(const char *dir)
{
    ndents = 0;
    long fd = sys_open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return -1;
    static char buf[16384];
    for (;;) {
        long n = sys_getdents64(fd, buf, sizeof buf);
        if (n <= 0) break;
        for (long o = 0; o < n;) {
            u16 reclen = *(u16 *)(buf + o + 16); char *nm = buf + o + 19; o += reclen;
            if (!scmp(nm, ".") || !scmp(nm, "..") || ndents >= MAXI) continue;
            struct dent *d = &dents[ndents]; scpy(d->name, nm, sizeof d->name);
            char p[512]; path_join(p, dir, nm, sizeof p);
            struct tas_stat64 st; memset(&st, 0, sizeof st);
            if (sys_lstat64(p, &st) == 0) { d->mode = st.st_mode; d->size = st.st_size; } else { d->mode = 0; d->size = 0; }
            ndents++;
        }
    }
    sys_close(fd);
    for (int i = 0; i < ndents; i++) sorted[i] = i;
    for (int i = 1; i < ndents; i++) { int v = sorted[i], j = i; while (j > 0 && dent_less(v, sorted[j - 1])) { sorted[j] = sorted[j - 1]; j--; } sorted[j] = v; }
    return 0;
}
static void mode_str(u32 m, char *b)
{
    const char *t = "?"; u32 f = m & 0170000;
    if (f == 0040000) t = "d"; else if (f == 0100000) t = "-"; else if (f == 0120000) t = "l";
    else if (f == 0020000) t = "c"; else if (f == 0060000) t = "b"; else if (f == 0010000) t = "p"; else if (f == 0140000) t = "s";
    scpy(b, t, 12);
    const char *r = "rwxrwxrwx"; for (int i = 0; i < 9; i++) { char c[2] = { (m >> (8 - i)) & 1 ? r[i] : '-', 0 }; scat(b, c, 12); }
}

static void file_actions(const char *dir, struct dent *d)
{
    char p[512], q[600], cmd[1400]; path_join(p, dir, d->name, sizeof p); shq(q, p, sizeof q);
    int isdir = (d->mode & 0170000) == 0040000, exe = d->mode & 0111;
    char info[64], sz[24], ms[12]; mode_str(d->mode, ms); fmt_size(d->size, sz);
    scpy(info, ms, sizeof info); scat(info, "  ", sizeof info); scat(info, sz, sizeof info);
    nitems = 0;
    if (!isdir) add('f', "View", info, 0, 1);
    add('m', "Copy", "to clipboard", 0, 2);
    add('m', "Cut", "move on paste", 0, 3);
    add('p', exe ? "Remove exec flag" : "Make executable", "chmod", 0, 4);
    if (!isdir && exe) add('a', "Run", "output is shown afterwards", 0, 6);
    add('r', "Delete", isdir ? "folder and everything inside" : "cannot be undone", C_RED, 5);
    int r = run_list(d->name, 1, 0);
    if (r < 0) return;
    int tag = items[r].tag;
    if (tag == 1) viewer(p);
    else if (tag == 2 || tag == 3) { scpy(clip, p, sizeof clip); clipcut = tag == 3; }
    else if (tag == 4) { scpy(cmd, exe ? "chmod a-x " : "chmod a+x ", sizeof cmd); scat(cmd, q, sizeof cmd); run_sh(cmd, 0); }
    else if (tag == 5) {
        if (!confirm("Delete?", d->name)) return;
        busy("deleting"); scpy(cmd, "rm -rf ", sizeof cmd); scat(cmd, q, sizeof cmd); scat(cmd, " && sync", sizeof cmd);
        if (run_sh(cmd, 0)) message("Delete", "failed", d->name, C_RED, 1);
    } else if (tag == 6) {
        busy("running");
        char dq[600]; shq(dq, dir, sizeof dq);
        scpy(cmd, "cd ", sizeof cmd); scat(cmd, dq, sizeof cmd); scat(cmd, " && ", sizeof cmd); scat(cmd, q, sizeof cmd);
        sys_open("/tmp/run.log", O_WRONLY | O_CREAT | O_TRUNC);
        run_sh(cmd, "/tmp/run.log");
        viewer("/tmp/run.log");
    }
}

static void explore(const char *start, const char *title)
{
    char dir[512]; scpy(dir, start, sizeof dir);
    int sel = 0;
    for (;;) {
        if (read_dir(dir) < 0) { message(title, "cannot open folder", dir, C_RED, 1); return; }
        nitems = 0;
        add('b', "..", dir, C_DIM, -1);
        if (clip[0]) add('p', clipcut ? "Move here" : "Paste here", sbase(clip), C_GRN, -2);
        add('p', "New folder", 0, C_DIM, -3);
        if (scmp(dir, start)) add('m', "This folder...", "copy, cut, delete, chmod", C_DIM, -4);
        int base = nitems;
        for (int i = 0; i < ndents && nitems < MAXI; i++) {
            struct dent *d = &dents[sorted[i]]; u32 f = d->mode & 0170000; char sub[64];
            if (f == 0040000) add('d', d->name, 0, 0, i);
            else { fmt_size(d->size, sub); add(f == 0120000 ? 'l' : 'f', d->name, sub, (d->mode & 0111) ? C_GRN : 0, i); }
        }
        int r = run_list(sbase(dir)[0] ? sbase(dir) : "/", 1, sel);
        if (r == -2) return;
        if (r < 0 || items[r].tag == -1) {
            if (!scmp(dir, start)) return;
            path_parent(dir); sel = 0; continue;
        }
        sel = r;
        char cmd[1400], a[600], b[600];
        if (items[r].tag == -2) {
            shq(a, clip, sizeof a); shq(b, dir, sizeof b);
            busy(clipcut ? "moving" : "copying");
            scpy(cmd, clipcut ? "mv " : "cp -a ", sizeof cmd); scat(cmd, a, sizeof cmd); scat(cmd, " ", sizeof cmd); scat(cmd, b, sizeof cmd);
            scat(cmd, "/ && sync", sizeof cmd);
            if (run_sh(cmd, 0)) message("Paste", "failed", sbase(clip), C_RED, 1);
            if (clipcut) clip[0] = 0;
            continue;
        }
        if (items[r].tag == -3) {
            for (int k = 1; k < 100; k++) {
                char p[512], n[8], nm[32] = "new_folder_"; utoa(k, n); scat(nm, n, sizeof nm); path_join(p, dir, nm, sizeof p);
                struct tas_stat64 st; if (sys_lstat64(p, &st) == 0) continue;
                sys_mkdir(p, 0755); break;
            }
            continue;
        }
        if (items[r].tag == -4) {
            struct dent self; memset(&self, 0, sizeof self);
            char parent[512]; scpy(parent, dir, sizeof parent); path_parent(parent);
            scpy(self.name, sbase(dir), sizeof self.name);
            struct tas_stat64 st;
            if (sys_lstat64(dir, &st) == 0) self.mode = st.st_mode;
            file_actions(parent, &self);
            if (sys_lstat64(dir, &st) != 0) { scpy(dir, parent, sizeof dir); sel = 0; }
            continue;
        }
        struct dent *d = &dents[sorted[items[r].tag]];
        (void)base;
        if ((d->mode & 0170000) == 0040000) {
            char p[512]; path_join(p, dir, d->name, sizeof p); scpy(dir, p, sizeof dir); sel = 0;
            /* a tap opens folders; hold/long-press style actions on a folder are under the '..' entry's parent */
            continue;
        }
        file_actions(dir, d);
    }
}

static void folder_menu(const char *dir, const char *name)
{
    /* actions for a folder chosen from a "places" screen */
    (void)dir; (void)name;
}

static void places(void)
{
    int sel = 0;
    for (;;) {
        nitems = 0;
        add('d', "Recovery root /", "RAM disk + /dev /proc /sys", 0, 100);
        add('d', "SD card", is_mounted("/mnt/sd") ? "/mnt/sd" : "not mounted - tap to mount", 0, 101);
        for (int i = 0; emmc_parts[i]; i++) {
            char mp[96] = "/mnt/emmc/", sub[96]; scat(mp, emmc_parts[i], sizeof mp);
            scpy(sub, is_mounted(mp) ? "mounted rw at " : "eMMC partition - tap to mount", sizeof sub);
            if (is_mounted(mp)) scat(sub, mp, sizeof sub);
            char lab[64] = "eMMC: "; scat(lab, emmc_parts[i], sizeof lab);
            add('d', lab, sub, 0, i);
        }
        add('x', "Unmount all eMMC", "sync and unmount /mnt/emmc/*", C_DIM, 102);
        int r = run_list("File explorer", 1, sel);
        if (r < 0) return;
        sel = r; int tag = items[r].tag;
        if (tag == 100) explore("/", "Recovery root");
        else if (tag == 101) {
            if (!is_mounted("/mnt/sd")) run_sh("tas-sd mount", 0);
            if (is_mounted("/mnt/sd")) explore("/mnt/sd", "SD card"); else message("SD card", "no SD card found", 0, C_RED, 1);
        } else if (tag == 102) { busy("unmounting"); run_sh("sync; for m in /mnt/emmc/*; do umount $m 2>/dev/null; done", 0); }
        else {
            char mp[96];
            if (mount_part(emmc_parts[tag], mp, sizeof mp)) { message(emmc_parts[tag], "cannot mount", "raw partition or unknown filesystem", C_RED, 1); continue; }
            explore(mp, emmc_parts[tag]);
        }
    }
    (void)folder_menu;
}

/* ------------------------------------------------------------------ app manager */
/* An app is a folder with app.conf:   name=Hello   exec=run.sh   desc=...   fullscreen=0|1
 * Installed apps live on eMMC: /mnt/emmc/userdata/tasapps/<id>/ ; installable ones on SD: /mnt/sd/tasapps/<id>/ */
struct app { char id[64], dir[256], name[64], exec[96], desc[64]; int full, installed; };
static struct app apps[64]; static int napps;

static void conf_get(const char *buf, const char *key, char *out, size_t n)
{
    out[0] = 0; size_t kl = tas_strlen(key);
    for (const char *s = buf; *s; ) {
        int match = 1; for (size_t i = 0; i < kl; i++) if (s[i] != key[i]) { match = 0; break; }
        if (match && s[kl] == '=') {
            s += kl + 1; size_t o = 0; while (*s && *s != '\n' && *s != '\r' && o + 1 < n) out[o++] = *s++; out[o] = 0; return;
        }
        while (*s && *s != '\n') s++;
        if (*s) s++;
    }
}
static void scan_apps(const char *root, int installed)
{
    if (read_dir(root) < 0) return;
    static struct dent tmp[64]; int nt = ndents > 64 ? 64 : ndents;
    for (int i = 0; i < nt; i++) tmp[i] = dents[sorted[i]];
    for (int i = 0; i < nt && napps < 64; i++) {
        if ((tmp[i].mode & 0170000) != 0040000) continue;
        struct app *a = &apps[napps]; memset(a, 0, sizeof *a);
        path_join(a->dir, root, tmp[i].name, sizeof a->dir);
        char cf[320]; path_join(cf, a->dir, "app.conf", sizeof cf);
        long f = sys_open(cf, O_RDONLY); if (f < 0) continue;
        char buf[1024]; long n = sys_read(f, buf, sizeof buf - 1); sys_close(f); buf[n > 0 ? n : 0] = 0;
        scpy(a->id, tmp[i].name, sizeof a->id);
        conf_get(buf, "name", a->name, sizeof a->name); if (!a->name[0]) scpy(a->name, a->id, sizeof a->name);
        conf_get(buf, "exec", a->exec, sizeof a->exec); if (!a->exec[0]) scpy(a->exec, "run.sh", sizeof a->exec);
        conf_get(buf, "desc", a->desc, sizeof a->desc);
        char fs[8]; conf_get(buf, "fullscreen", fs, sizeof fs); a->full = fs[0] == '1';
        a->installed = installed;
        int dup = 0; for (int k = 0; k < napps; k++) if (!scmp(apps[k].id, a->id)) dup = 1;
        if (!dup) napps++;
    }
}
static void run_app(struct app *a)
{
    char cmd[800], d[300], e[300];
    shq(d, a->dir, sizeof d); path_join(cmd, a->dir, a->exec, sizeof cmd); shq(e, cmd, sizeof e);
    char ev[300] = "TAS_APPDIR="; scat(ev, a->dir, sizeof ev); envp[2] = ev; envp[3] = "TAS_FB=/dev/fb0";
    scpy(cmd, "cd ", sizeof cmd); scat(cmd, d, sizeof cmd); scat(cmd, " && sh ", sizeof cmd); scat(cmd, e, sizeof cmd);
    long t = sys_open("/tmp/app.log", O_WRONLY | O_CREAT | O_TRUNC); if (t >= 0) sys_close(t);
    if (a->full) {   /* hand the display and input over to the app */
        input_close(); fb_close();
        run_sh(cmd, "/tmp/app.log");
        fb_open(); input_open();
    } else {
        busy(a->name);
        run_sh(cmd, "/tmp/app.log");
        viewer("/tmp/app.log");
    }
    envp[2] = envp[3] = 0;
}
static void app_manager(void)
{
    int sel = 0;
    for (;;) {
        char ud[96];
        mount_part("userdata", ud, sizeof ud);
        sys_mkdir("/mnt/emmc/userdata/tasapps", 0755);
        if (!is_mounted("/mnt/sd")) run_sh("tas-sd mount", 0);
        napps = 0;
        scan_apps("/mnt/emmc/userdata/tasapps", 1);
        scan_apps("/mnt/sd/tasapps", 0);
        nitems = 0;
        for (int i = 0; i < napps; i++) {
            char sub[64]; scpy(sub, apps[i].installed ? "installed  " : "on SD card  ", sizeof sub); scat(sub, apps[i].desc, sizeof sub);
            add('a', apps[i].name, sub, apps[i].installed ? 0 : C_DIM, i);
        }
        if (!napps) add('x', "No apps", "put apps in SD:/tasapps/<id>/app.conf", C_DIM, -1);
        int r = run_list("App manager", 1, sel);
        if (r < 0) return;
        sel = r; if (items[r].tag < 0) continue;
        struct app *a = &apps[items[r].tag];
        nitems = 0;
        add('a', "Run", a->full ? "full screen app" : "output is shown afterwards", C_GRN, 1);
        if (!a->installed) add('p', "Install to eMMC", "copy to userdata/tasapps", 0, 2);
        else add('r', "Uninstall", "remove from eMMC", C_RED, 3);
        add('f', "Last output", "/tmp/app.log", 0, 4);
        add('d', "Open folder", a->dir, 0, 5);
        int k = run_list(a->name, 1, 0);
        if (k < 0) continue;
        int t = items[k].tag; char cmd[900], s[300];
        if (t == 1) run_app(a);
        else if (t == 2) {
            busy("installing"); shq(s, a->dir, sizeof s);
            scpy(cmd, "cp -a ", sizeof cmd); scat(cmd, s, sizeof cmd); scat(cmd, " /mnt/emmc/userdata/tasapps/ && sync", sizeof cmd);
            if (run_sh(cmd, 0)) message("Install", "failed", a->name, C_RED, 1);
        } else if (t == 3) {
            if (!confirm("Uninstall?", a->name)) continue;
            busy("removing"); shq(s, a->dir, sizeof s);
            scpy(cmd, "rm -rf ", sizeof cmd); scat(cmd, s, sizeof cmd); scat(cmd, " && sync", sizeof cmd); run_sh(cmd, 0);
        } else if (t == 4) viewer("/tmp/app.log");
        else if (t == 5) explore(a->dir, a->name);
    }
}

/* ------------------------------------------------------------------ home */
static void read_line(const char *path, char *out, size_t n)
{
    out[0] = 0; long f = sys_open(path, O_RDONLY); if (f < 0) return;
    long r = sys_read(f, out, n - 1); sys_close(f); if (r < 0) r = 0; out[r] = 0;
    for (long i = 0; i < r; i++) if (out[i] == '\n') { out[i] = 0; break; }
}
static void power(const char *how)
{
    message("TasRecovery", !scmp(how, "poweroff") ? "Powering off..." : "Rebooting...", how, C_FG, 0);
    run_sh("sync; for m in /mnt/emmc/*; do umount $m 2>/dev/null; done; tas-sd umount", 0);
    char cmd[64] = "tas-reboot "; scat(cmd, how, sizeof cmd); run_sh(cmd, 0);
}
static void sd_as_usb(void)
{
    if (run_sh("tas-ums on", 0)) { message("USB drive", "no SD card found", 0, C_RED, 1); return; }
    message("USB drive", "SD card is a USB drive", "eject it on the PC, then tap here", C_ACC, 1);
    run_sh("tas-ums off", 0);
}
static void home(void)
{
    int sel = 0;
    for (;;) {
        char bat[16], st[24], sub[64];
        read_line("/sys/class/power_supply/battery/capacity", bat, sizeof bat);
        read_line("/sys/class/power_supply/battery/status", st, sizeof st);
        scpy(sub, "battery ", sizeof sub); scat(sub, bat, sizeof sub); scat(sub, "% ", sizeof sub); scat(sub, st, sizeof sub);
        scat(sub, is_mounted("/mnt/sd") ? "  SD ok" : "  no SD", sizeof sub);
        nitems = 0;
        add('d', "File explorer", "root, eMMC partitions, SD card", 0, 1);
        add('a', "App manager", "run, install, remove apps", 0, 2);
        add('m', "SD card as USB drive", "adb stays available", 0, 3);
        add('p', "Backup to SD card", "all partitions except big ones", 0, 4);
        scpy(bar_status, bat[0] ? bat : "?", sizeof bar_status); scat(bar_status, "%", sizeof bar_status);
        scat(bar_status, is_mounted("/mnt/sd") ? " SD" : "", sizeof bar_status);
        (void)sub;
        add('r', "Reboot system", "back to the GoPro firmware", 0, 5);
        add('r', "Reboot to bootloader", "fastboot", 0, 6);
        add('x', "Power off", 0, 0, 7);
        int r = run_list("TasRecovery", 0, sel);
        bar_status[0] = 0;
        if (r == -2) return;
        if (r < 0) continue;
        sel = r;
        switch (items[r].tag) {
        case 1: places(); break;
        case 2: app_manager(); break;
        case 3: sd_as_usb(); break;
        case 4:
            if (!confirm("Backup?", "writes TASBACKUP/<date> on SD")) break;
            busy("backup to SD card");
            if (run_sh("tas-backup", "/tmp/tasrec.log")) message("Backup", "FAILED", "see /tmp/tasrec.log", C_RED, 1);
            else message("Backup", "done", "SD:/TASBACKUP", C_GRN, 1);
            break;
        case 5: power("system"); break;
        case 6: power("bootloader"); break;
        case 7: power("poweroff"); break;
        }
    }
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!scmp(argv[i], "--fb") && i + 1 < argc) fbpath = argv[++i];
        else if (!scmp(argv[i], "--shot") && i + 1 < argc) shotdir = argv[++i];
    }
    logfd = sys_open("/tmp/tasui.log", O_WRONLY | O_CREAT | O_APPEND);
    if (shotdir) {   /* render the main screens for documentation / host testing */
        home(); places(); explore("/", "root"); nitems = 0;
        message("Backup", "done", "SD:/TASBACKUP", C_GRN, 0);
        return 0;
    }
    if (fb_open()) logs("no framebuffer, running blind", 0);
    input_open();
    home();
    return 0;
}
