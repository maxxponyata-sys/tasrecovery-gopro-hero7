/* tasbtn - "recovery button" for the stock GoPro HERO7 firmware.
 *
 * Runs next to gpcam (evdev allows several readers) and watches the PMIC keys:
 *   744 = side (mode/power) button, 745 = top (shutter) button.
 * Pressing the side button N times (default 5) within the window (default 3 s) starts a
 * 3 second countdown with the front/back LEDs blinking; any key press cancels it.
 * Otherwise the camera syncs and reboots into recovery (reboot(RESTART2, "recovery")).
 *
 * Usage: tasbtn [-k keycode] [-n presses] [-t window_ms] [-d /dev/input/eventX] [-r reason]
 */
#define TAS_MAIN
#include "sys.h"

#define EV_KEY 1
struct pollfd { int fd; short events, revents; };
struct ev { u32 s, us; u16 type, code; int value; };

static int atoi_(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return v; }

static void led(int on)
{
    const char *p[] = { "/sys/class/leds/front_led/brightness", "/sys/class/leds/back_led/brightness" };
    for (int i = 0; i < 2; i++) {
        long f = sys_open(p[i], O_WRONLY);
        if (f >= 0) { sys_write(f, on ? "255" : "0", on ? 3 : 1); sys_close(f); }
    }
}
static void logk(const char *m)
{
    long f = sys_open("/dev/kmsg", O_WRONLY);
    if (f >= 0) { tas_puts(f, "tasbtn: "); tas_puts(f, m); tas_puts(f, "\n"); sys_close(f); }
}

/* returns 1 if a key went down within ms milliseconds */
static int key_within(int fd, int ms)
{
    long long end = tas_ms() + ms;
    for (;;) {
        long left = (long)(end - tas_ms());
        if (left <= 0) return 0;
        struct pollfd p = { fd, 1, 0 };
        if (sys_poll(&p, 1, left) <= 0) continue;
        struct ev e[16]; long n = sys_read(fd, e, sizeof e);
        for (int i = 0; i < n / 16; i++) if (e[i].type == EV_KEY && e[i].value == 1) return 1;
    }
}

int main(int argc, char **argv)
{
    int key = 744, need = 5, window = 3000;
    const char *dev = "/dev/input/event0", *reason = "recovery";
    for (int i = 1; i + 1 < argc; i++) {
        if (argv[i][0] != '-') continue;
        switch (argv[i][1]) {
        case 'k': key = atoi_(argv[++i]); break;
        case 'n': need = atoi_(argv[++i]); break;
        case 't': window = atoi_(argv[++i]); break;
        case 'd': dev = argv[++i]; break;
        case 'r': reason = argv[++i]; break;
        }
    }
    long fd = sys_open(dev, O_RDONLY);
    if (fd < 0) { logk("cannot open input device"); return 1; }
    logk("watching for the recovery gesture");
    long long t[16]; int cnt = 0;
    if (need > 16) need = 16;
    for (;;) {
        struct ev e[16];
        long n = sys_read(fd, e, sizeof e);
        if (n <= 0) { sys_sleep(1, 0); continue; }
        for (int i = 0; i < n / 16; i++) {
            if (e[i].type != EV_KEY || e[i].value != 1) continue;
            if (e[i].code != key) { cnt = 0; continue; }
            long long now = tas_ms();
            t[cnt % 16] = now; cnt++;
            if (cnt >= need && now - t[(cnt - need) % 16] <= window) {
                cnt = 0;
                logk("gesture detected, rebooting to recovery in 3 s (press a key to cancel)");
                int cancelled = 0;
                for (int k = 0; k < 6 && !cancelled; k++) { led(k % 2 == 0); cancelled = key_within(fd, 500); }
                led(0);
                if (cancelled) { logk("cancelled"); continue; }
                sys_sync();
                sys6(__NR_reboot, (long)0xfee1dead, 672274793L, (long)0xa1b2c3d4, (long)reason, 0, 0);
                logk("reboot failed");
            }
        }
    }
}
