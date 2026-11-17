/* tasdbg - debug screen for the stock GoPro HERO7 firmware.
 *
 *   tasdbg watch [script]   daemon: a triple press of the side button (744) runs the script
 *                           (default /usr/bin/tasdbg.sh). 4+ presses are ignored - 5 presses belong to tasbtn.
 *   tasdbg ui [--fb PATH]   draw the debug screen (landscape) and refresh it every second until any button
 *                           or a touch; the caller must have freed the display (gpcam + qmmf-server stopped).
 *   tasdbg shot FILE        render one frame (320x480 RGBA) to FILE for previews.
 */
#define TAS_MAIN
#include "sys.h"
#include "font24.h"
#include "font16.h"

#define PW 320
#define PH 480
#define LW 480
#define LH 320
#define KEY_SIDE 744
#define KEY_TOP  745

/* ------------------------------------------------------------------ helpers */
static void scpy(char *d, const char *s, size_t n) { size_t i = 0; if (!n) return; for (; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = 0; }
static void scat(char *d, const char *s, size_t n) { size_t l = tas_strlen(d); if (l < n) scpy(d + l, s, n - l); }
static int sstarts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static void utoa(unsigned long long v, char *b) { char t[24]; int i = 0; do { t[i++] = '0' + v % 10; v /= 10; } while (v); int j = 0; while (i) b[j++] = t[--i]; b[j] = 0; }
static long atol_(const char *s) { long v = 0; int neg = 0; while (*s == ' ') s++; if (*s == '-') { neg = 1; s++; } while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return neg ? -v : v; }
static void catn(char *d, unsigned long long v, size_t n) { char t[24]; utoa(v, t); scat(d, t, n); }
static void catd1(char *d, long tenths, size_t n)   /* 1 decimal */
{
    if (tenths < 0) { scat(d, "-", n); tenths = -tenths; }
    catn(d, tenths / 10, n); char t[3] = { '.', (char)('0' + tenths % 10), 0 }; scat(d, t, n);
}
static int readf(const char *p, char *b, size_t n)
{
    long f = sys_open(p, O_RDONLY); if (f < 0) { b[0] = 0; return -1; }
    long r = sys_read(f, b, n - 1); sys_close(f); if (r < 0) r = 0; b[r] = 0;
    for (long i = 0; i < r; i++) if (b[i] == '\n') { b[i] = 0; break; }
    return (int)r;
}
static long readl(const char *p) { char b[64]; if (readf(p, b, sizeof b) <= 0) return -1; return atol_(b); }

/* ------------------------------------------------------------------ drawing (landscape on a portrait panel) */
static u32 back[PW * PH];
#define RGB(r, g, b) (0xff000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))
#define C_BG   RGB(10, 12, 16)
#define C_BAR  RGB(160, 30, 40)
#define C_FG   RGB(232, 234, 239)
#define C_DIM  RGB(130, 138, 150)
#define C_GRN  RGB(60, 200, 110)
#define C_YEL  RGB(240, 190, 60)
#define C_RED  RGB(240, 80, 80)
#define C_ACC  RGB(26, 161, 232)

static inline void plot(int lx, int ly, u32 c)   /* landscape -> panel, same mapping as the Doom port */
{
    if ((unsigned)lx >= LW || (unsigned)ly >= LH) return;
    back[lx * PW + (PW - 1 - ly)] = c;
}
static void fillr(int x, int y, int w, int h, u32 c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) plot(x + i, y + j, c); }
static int text(int x, int y, const char *s, u32 c, int big)
{
    int fw = big ? FONT24_W : FONT16_W, fh = big ? FONT24_H : FONT16_H, rb = big ? FONT24_ROWBYTES : FONT16_ROWBYTES;
    for (; *s && x + fw <= LW; s++, x += fw) {
        int ch = (u8)*s; if (ch < 32 || ch > 126) ch = '?';
        const u8 *g = big ? font24_data[ch - 32] : font16_data[ch - 32];
        for (int r = 0; r < fh; r++)
            for (int b = 0; b < fw; b++)
                if (g[r * rb + b / 8] & (0x80 >> (b % 8))) plot(x + b, y + r, c);
    }
    return x;
}
static int line_y;
static void row(const char *label, const char *val, u32 c)
{
    text(8, line_y, label, C_DIM, 0);
    text(8 + 11 * FONT16_W, line_y, val, c, 0);
    line_y += FONT16_H + 2;
}

/* ------------------------------------------------------------------ info */
static void firmware(char *out, size_t n)
{
    static char buf[1024];
    out[0] = 0;
    if (readf("/mnt/sdcard/MISC/version.txt", buf, sizeof buf) < 0 && readf("/tmp/fuse_d/MISC/version.txt", buf, sizeof buf) < 0) {
        scpy(out, "?", n); return;
    }
    /* whole file: re-read without the newline cut */
    long f = sys_open("/mnt/sdcard/MISC/version.txt", O_RDONLY); if (f < 0) f = sys_open("/tmp/fuse_d/MISC/version.txt", O_RDONLY);
    long r = f >= 0 ? sys_read(f, buf, sizeof buf - 1) : 0; if (f >= 0) sys_close(f); if (r < 0) r = 0; buf[r] = 0;
    const char *keys[] = { "\"firmware version\":\"", "\"camera type\":\"" };
    for (int k = 0; k < 2; k++) {
        for (char *s = buf; *s; s++) if (sstarts(s, keys[k])) {
            s += tas_strlen(keys[k]); char v[48]; int i = 0;
            while (*s && *s != '"' && i < 47) v[i++] = *s++;
            v[i] = 0;
            if (out[0]) scat(out, "  ", n);
            scat(out, v, n); break;
        }
    }
    if (!out[0]) scpy(out, "?", n);
}

static const char *gp_daemons[] = { "gpNet", "gpCamApi", "gpAth", "gpUdp", "gpPlatform", "gpHttpServer", "gpControlD",
                                    "babyblue", "gpBtMan", "gpBleSdkD", "gpTunnelD", "womDaemon", "gpWakeService", 0 };
static int proc_running(const char *name)
{
    static char buf[8192]; char path[48], comm[32];
    long fd = sys_open("/proc", O_RDONLY | O_DIRECTORY); if (fd < 0) return 0;
    int found = 0;
    for (;;) {
        long n = sys_getdents64(fd, buf, sizeof buf); if (n <= 0) break;
        for (long o = 0; o < n && !found;) {
            u16 reclen = *(u16 *)(buf + o + 16); char *nm = buf + o + 19; o += reclen;
            if (nm[0] < '0' || nm[0] > '9') continue;
            scpy(path, "/proc/", sizeof path); scat(path, nm, sizeof path); scat(path, "/comm", sizeof path);
            if (readf(path, comm, sizeof comm) > 0) {
                size_t l = tas_strlen(name); if (l > 15) l = 15;
                int eq = 1; for (size_t i = 0; i < l; i++) if (comm[i] != name[i]) { eq = 0; break; }
                if (eq && comm[l] == 0) found = 1;
            }
        }
        if (found) break;
    }
    sys_close(fd);
    return found;
}

static long long boot_ms;
static void draw_screen(int frame)
{
    char v[96], b[96];
    for (int i = 0; i < PW * PH; i++) back[i] = C_BG;
    fillr(0, 0, LW, 34, C_BAR);
    text(8, 5, "TASMANIA DEBUG", C_FG, 1);
    { char t[16] = ""; long long s = (tas_ms() - boot_ms) / 1000; catn(t, s, sizeof t); scat(t, "s", sizeof t);
      text(LW - 8 - (int)tas_strlen(t) * FONT16_W, 10, t, C_FG, 0); }
    line_y = 42;

    firmware(v, sizeof v); row("firmware", v, C_FG);
    readf("/proc/version", b, sizeof b); { char *s = b; int sp = 0; while (*s && sp < 2) if (*s++ == ' ') sp++; char k[32]; int i = 0; while (*s && *s != ' ' && i < 31) k[i++] = *s++; k[i] = 0; scpy(v, k, sizeof v); }
    row("kernel", v, C_FG);

    readf("/proc/uptime", b, sizeof b); { long up = atol_(b); v[0] = 0; catn(v, up / 3600, sizeof v); scat(v, "h ", sizeof v); catn(v, up / 60 % 60, sizeof v); scat(v, "m ", sizeof v); catn(v, up % 60, sizeof v); scat(v, "s   load ", sizeof v);
      char la[64]; readf("/proc/loadavg", la, sizeof la); int sp = 0; for (int i = 0; la[i]; i++) if (la[i] == ' ' && ++sp == 3) { la[i] = 0; break; } scat(v, la, sizeof v); }
    row("uptime", v, C_FG);

    { long cap = readl("/sys/class/power_supply/battery/capacity"), t = readl("/sys/class/power_supply/battery/temp");
      char st[24]; readf("/sys/class/power_supply/battery/status", st, sizeof st);
      v[0] = 0; catn(v, cap < 0 ? 0 : cap, sizeof v); scat(v, "%  ", sizeof v); scat(v, st, sizeof v); scat(v, "  ", sizeof v);
      if (t > -1000 && t != -1) { catd1(v, t, sizeof v); scat(v, " C", sizeof v); }   /* -1 = no sensor file */
      row("battery", v, cap < 15 ? C_RED : cap < 40 ? C_YEL : C_GRN); }

    { long mn = 1L << 30, mx = 0; int on = 0;
      for (int c = 0; c < 8; c++) { char p[64] = "/sys/devices/system/cpu/cpu"; char d[4] = { (char)('0' + c), 0 }; scat(p, d, sizeof p); scat(p, "/cpufreq/scaling_cur_freq", sizeof p);
        long f = readl(p); if (f > 0) { on++; if (f < mn) mn = f; if (f > mx) mx = f; } }
      v[0] = 0; catn(v, on, sizeof v); scat(v, " online  ", sizeof v);
      if (on) { catn(v, mn / 1000, sizeof v); scat(v, "-", sizeof v); catn(v, mx / 1000, sizeof v); scat(v, " MHz", sizeof v); }
      row("cpu", v, C_FG); }

    { static char mi[2048]; long f = sys_open("/proc/meminfo", O_RDONLY); long r = f >= 0 ? sys_read(f, mi, sizeof mi - 1) : 0; if (f >= 0) sys_close(f); mi[r > 0 ? r : 0] = 0;
      long tot = 0, av = 0; for (char *s = mi; *s; s++) { if (sstarts(s, "MemTotal:")) tot = atol_(s + 9); if (sstarts(s, "MemAvailable:")) av = atol_(s + 13); }
      v[0] = 0; catn(v, (tot - av) / 1024, sizeof v); scat(v, " / ", sizeof v); catn(v, tot / 1024, sizeof v); scat(v, " MB used", sizeof v);
      row("memory", v, C_FG); }

    { long st[30]; memset(st, 0, sizeof st);
      long r = sys6(266, (long)"/mnt/sdcard", 84, (long)st, 0, 0, 0);   /* statfs64 */
      /* ARM statfs64 (packed, aligned 4): f_type@0 f_bsize@4 f_blocks@8 f_bfree@16 f_bavail@24 */
      if (r == 0) { unsigned long long bs = (unsigned long)st[1], blocks, bfree;
        memcpy(&blocks, (u8 *)st + 8, 8); memcpy(&bfree, (u8 *)st + 24, 8);
        v[0] = 0; catn(v, bfree * bs >> 20, sizeof v); scat(v, " MB free of ", sizeof v); catn(v, blocks * bs >> 20, sizeof v); scat(v, " MB", sizeof v); }
      else scpy(v, "not mounted", sizeof v);
      row("sd card", v, r == 0 ? C_FG : C_YEL); }

    { const char *ifs[] = { "wlan0", "softap.0", "rndis0", 0 }; v[0] = 0;
      for (int i = 0; ifs[i]; i++) { char p[64] = "/sys/class/net/"; scat(p, ifs[i], sizeof p); scat(p, "/operstate", sizeof p);
        char s[16]; if (readf(p, s, sizeof s) > 0) { if (v[0]) scat(v, "  ", sizeof v); scat(v, ifs[i], sizeof v); scat(v, ":", sizeof v); scat(v, s, sizeof v); } }
      if (!v[0]) scpy(v, "no interfaces", sizeof v);
      row("network", v, C_FG); }

    { v[0] = 0; int shown = 0;
      for (int z = 0; z < 12 && shown < 4; z++) { char p[64] = "/sys/class/thermal/thermal_zone", d[4]; utoa(z, d); scat(p, d, sizeof p);
        char tp[64]; scpy(tp, p, sizeof tp); scat(tp, "/temp", sizeof tp); long t = readl(tp);
        if (t <= 0) continue;
        if (t > 1000) t /= 100;   /* milli-degrees -> tenths */
        else t *= 10;
        if (v[0]) scat(v, "  ", sizeof v);
        catd1(v, t, sizeof v); shown++; }
      scat(v, " C", sizeof v);
      row("temps", v, C_FG); }

    { int up = 0, tot = 0; char miss[64] = "";
      for (int i = 0; gp_daemons[i]; i++) { tot++; if (proc_running(gp_daemons[i])) up++; else if (tas_strlen(miss) < 40) { if (miss[0]) scat(miss, ",", sizeof miss); scat(miss, gp_daemons[i], sizeof miss); } }
      v[0] = 0; catn(v, up, sizeof v); scat(v, "/", sizeof v); catn(v, tot, sizeof v); scat(v, " running", sizeof v);
      if (miss[0]) { scat(v, "  -", sizeof v); scat(v, miss, sizeof v); }
      row("gopro", v, up == tot ? C_GRN : C_YEL);
      row("ui", "gpcam + qmmf-server stopped for this screen", C_DIM); }

    fillr(0, LH - 24, LW, 24, RGB(24, 28, 36));
    text(8, LH - 20, frame % 2 ? "any button: reboot back to GoPro" : "any button: reboot back to GoPro.", C_ACC, 0);
}

/* ------------------------------------------------------------------ framebuffer */
static int fbfd = -1; static u8 *fbmem; static u32 fbline = PW * 4, var[40], fix[17]; static int lit;
static int fb_open(const char *path)
{
    fbfd = sys_open(path, O_RDWR);
    if (fbfd < 0) return -1;
    sys_ioctl(fbfd, 0x4611, 0);
    sys_ioctl(fbfd, 0x4602, fix); sys_ioctl(fbfd, 0x4600, var);
    var[4] = var[5] = 0; var[21] = 0; sys_ioctl(fbfd, 0x4601, var);
    sys_ioctl(fbfd, 0x4602, fix);
    if (fix[11] >= PW * 4 && fix[11] <= PW * 16) fbline = fix[11];
    size_t len = fix[5] ? fix[5] : (size_t)fbline * PH * 2;
    fbmem = sys_mmap2(0, len, 3, 1, fbfd, 0);
    if ((unsigned long)fbmem >= 0xfffff000UL) { fbmem = 0; return -1; }
    return 0;
}
static void fb_flush(void)
{
    if (!fbmem) return;
    for (int y = 0; y < PH; y++) memcpy(fbmem + (size_t)y * fbline, back + y * PW, PW * 4);
    var[4] = var[5] = 0; var[21] = 16 | 128; sys_ioctl(fbfd, 0x4601, var);
    var[21] = 16; sys_ioctl(fbfd, 0x4606, var);
    if (!lit) { long b = sys_open("/sys/class/leds/wled/brightness", O_WRONLY); if (b >= 0) { sys_write(b, "2048", 4); sys_close(b); } lit = 1; }
}

/* ------------------------------------------------------------------ input */
struct pollfd { int fd; short events, revents; };
struct ev { u32 s, us; u16 type, code; int value; };
static struct pollfd pfd[8]; static int npfd;
static void input_open(int nonblock)
{
    npfd = 0;
    for (int i = 0; i < 8; i++) {
        char p[32] = "/dev/input/event"; char d[4]; utoa(i, d); scat(p, d, sizeof p);
        long fd = sys_open(p, O_RDONLY | (nonblock ? 04000 : 0));
        if (fd >= 0) { pfd[npfd].fd = fd; pfd[npfd].events = 1; npfd++; }
    }
}
static void input_drain(void)
{
    struct ev e[32];
    for (int i = 0; i < npfd; i++) while (sys_read(pfd[i].fd, e, sizeof e) > 0) {}
}
/* waits up to ms for a button press or a touch; returns 1 if one happened */
static int wait_press(int ms)
{
    long long end = tas_ms() + ms;
    for (;;) {
        long left = (long)(end - tas_ms()); if (left <= 0) return 0;
        for (int i = 0; i < npfd; i++) pfd[i].revents = 0;
        if (sys_poll(pfd, npfd, left) <= 0) continue;
        for (int i = 0; i < npfd; i++) {
            if (!(pfd[i].revents & 1)) continue;
            struct ev e[32]; long n = sys_read(pfd[i].fd, e, sizeof e);
            for (int k = 0; k < n / 16; k++)
                if (e[k].type == 1 && e[k].value == 1) return 1;   /* any key, incl. BTN_TOUCH */
        }
    }
}

static int ui(const char *fbpath)
{
    boot_ms = tas_ms();
    if (fb_open(fbpath)) { tas_puts(2, "tasdbg: cannot open framebuffer\n"); return 1; }
    input_open(1);
    input_drain();
    for (int frame = 0; frame < 600; frame++) {          /* 10 minutes max */
        draw_screen(frame);
        fb_flush();
        if (wait_press(1000)) break;
    }
    return 0;
}

/* ------------------------------------------------------------------ watcher */
static void run_script(const char *script)
{
    long pid = sys_fork();
    if (pid == 0) {
        char *argv[] = { "sh", (char *)script, 0 };
        char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", 0 };
        sys_execve("/bin/sh", argv, envp);
        sys_exit(127);
    }
    if (pid > 0) { int st; sys_wait4(pid, &st); }
}

static int watch(const char *script)
{
    long fd = sys_open("/dev/input/event0", O_RDONLY);
    if (fd < 0) return 1;
    long long t[8]; int cnt = 0;
    for (;;) {
        struct pollfd p = { (int)fd, 1, 0 };
        int timeout = cnt >= 3 ? 700 : -1;      /* after the 3rd press: wait to see if a 4th comes */
        long r = sys_poll(&p, 1, timeout);
        if (r == 0) {                           /* quiet after exactly 3 quick presses */
            if (cnt == 3) {
                run_script(script);
                struct ev e[32]; long fl = sys_open("/dev/input/event0", O_RDONLY | 04000);
                if (fl >= 0) { while (sys_read(fl, e, sizeof e) > 0) {} sys_close(fl); }
                sys_close(fd); fd = sys_open("/dev/input/event0", O_RDONLY);   /* drop events queued meanwhile */
            }
            cnt = 0; continue;
        }
        if (r < 0) { sys_sleep(1, 0); continue; }
        struct ev e[16]; long n = sys_read(fd, e, sizeof e);
        for (int k = 0; k < n / 16; k++) {
            if (e[k].type != 1 || e[k].value != 1) continue;
            if (e[k].code != KEY_SIDE) { cnt = 0; continue; }
            long long now = tas_ms();
            if (cnt > 0 && now - t[(cnt - 1) % 8] > 600) cnt = 0;   /* presses must be < 0.6 s apart */
            t[cnt % 8] = now; cnt++;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc >= 2 && argv[1][0] == 'w') return watch(argc >= 3 ? argv[2] : "/usr/bin/tasdbg.sh");
    if (argc >= 2 && argv[1][0] == 'u') return ui(argc >= 4 ? argv[3] : "/dev/graphics/fb0");
    if (argc >= 3 && argv[1][0] == 's') {
        boot_ms = tas_ms(); draw_screen(0);
        long f = sys_open(argv[2], O_WRONLY | O_CREAT | O_TRUNC); if (f < 0) return 1;
        sys_write(f, back, sizeof back); sys_close(f); return 0;
    }
    tas_puts(2, "usage: tasdbg watch [script] | ui [--fb PATH] | shot FILE\n");
    return 1;
}
