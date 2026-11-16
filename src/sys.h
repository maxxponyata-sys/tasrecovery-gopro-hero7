/* Minimal freestanding Linux/ARM (EABI) runtime: syscalls, _start, a few libc bits.
 * Build: arm-none-eabi-gcc -marm -march=armv7-a -mfloat-abi=soft -nostdlib -static -ffreestanding */
#ifndef TAS_SYS_H
#define TAS_SYS_H

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned long size_t;

#define __NR_exit        1
#define __NR_read        3
#define __NR_write       4
#define __NR_open        5
#define __NR_close       6
#define __NR_sync       36
#define __NR_ioctl      54
#define __NR_reboot     88
#define __NR_munmap     91
#define __NR_nanosleep 162
#define __NR_mmap2     192

#define __NR_fork        2
#define __NR_unlink     10
#define __NR_execve     11
#define __NR_chdir      12
#define __NR_mount      21
#define __NR_kill       37
#define __NR_mkdir      39
#define __NR_dup2       63
#define __NR_setsid     66
#define __NR_umount2    52
#define __NR_wait4     114
#define __NR_poll      168
#define __NR_stat64    195
#define __NR_lstat64   196
#define __NR_getdents64 217
#define __NR_clock_gettime 263

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0100
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_DIRECTORY 040000

static inline long sys6(long n, long a, long b, long c, long d, long e, long f)
{
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    register long r4 __asm__("r4") = e;
    register long r5 __asm__("r5") = f;
    register long r7 __asm__("r7") = n;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7) : "memory");
    return r0;
}
#define sys3(n, a, b, c) sys6(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)

static inline long sys_read(int fd, void *b, size_t n)        { return sys3(__NR_read, fd, b, n); }
static inline long sys_write(int fd, const void *b, size_t n) { return sys3(__NR_write, fd, b, n); }
static inline long sys_open(const char *p, int fl)            { return sys3(__NR_open, p, fl, 0644); }
static inline long sys_close(int fd)                          { return sys3(__NR_close, fd, 0, 0); }
static inline long sys_ioctl(int fd, u32 req, void *arg)      { return sys3(__NR_ioctl, fd, req, arg); }
static inline void sys_sync(void)                             { sys3(__NR_sync, 0, 0, 0); }
static inline void *sys_mmap2(void *a, size_t len, int prot, int fl, int fd, long pgoff)
{
    return (void *)sys6(__NR_mmap2, (long)a, (long)len, prot, fl, fd, pgoff);
}
static inline __attribute__((noreturn)) void sys_exit(int c)
{
    for (;;) sys3(__NR_exit, c, 0, 0);
}
static inline void sys_sleep(long sec, long nsec)
{
    long ts[2] = { sec, nsec };
    sys3(__NR_nanosleep, ts, 0, 0);
}
static inline long sys_fork(void)                               { return sys3(__NR_fork, 0, 0, 0); }
static inline long sys_execve(const char *p, char *const *a, char *const *e) { return sys3(__NR_execve, p, a, e); }
static inline long sys_wait4(long pid, int *st)                 { return sys6(__NR_wait4, pid, (long)st, 0, 0, 0, 0); }
static inline long sys_chdir(const char *p)                     { return sys3(__NR_chdir, p, 0, 0); }
static inline long sys_mkdir(const char *p, int mode)           { return sys3(__NR_mkdir, p, mode, 0); }
static inline long sys_dup2(int a, int b)                       { return sys3(__NR_dup2, a, b, 0); }
static inline long sys_setsid(void)                             { return sys3(__NR_setsid, 0, 0, 0); }
static inline long sys_kill(long pid, int sig)                  { return sys3(__NR_kill, pid, sig, 0); }
static inline long sys_mount(const char *s, const char *t, const char *fs, unsigned long fl, const void *d)
{
    return sys6(__NR_mount, (long)s, (long)t, (long)fs, (long)fl, (long)d, 0);
}
static inline long sys_umount(const char *t)                    { return sys3(__NR_umount2, t, 0, 0); }
static inline long sys_getdents64(int fd, void *b, size_t n)    { return sys3(__NR_getdents64, fd, b, n); }
static inline long sys_poll(void *fds, int n, int timeout_ms)   { return sys3(__NR_poll, fds, n, timeout_ms); }

/* ARM EABI struct stat64 (104 bytes) - only the fields we use */
struct tas_stat64 {
    unsigned long long st_dev; unsigned char pad0[4]; u32 st_ino_lo;
    u32 st_mode, st_nlink, st_uid, st_gid;
    unsigned long long st_rdev; unsigned char pad3[4];
    long long st_size;
    u32 st_blksize; u32 pad4;
    unsigned long long st_blocks;
    u32 st_atime, st_atime_ns, st_mtime, st_mtime_ns, st_ctime, st_ctime_ns;
    unsigned long long st_ino;
};
static inline long sys_lstat64(const char *p, struct tas_stat64 *s) { return sys3(__NR_lstat64, p, s, 0); }
static inline long sys_stat64(const char *p, struct tas_stat64 *s)  { return sys3(__NR_stat64, p, s, 0); }

/* monotonic milliseconds */
static inline long long tas_ms(void)
{
    long ts[2] = { 0, 0 };
    sys3(__NR_clock_gettime, 1, ts, 0);
    return (long long)ts[0] * 1000 + ts[1] / 1000000;
}

/* libc bits gcc may emit calls to */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);

static inline size_t tas_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static inline void tas_puts(int fd, const char *s) { sys_write(fd, s, tas_strlen(s)); }
static inline void tas_putnum(int fd, long v)
{
    char b[16]; int i = 15; int neg = v < 0; unsigned long u = neg ? -v : v;
    b[i] = 0;
    do { b[--i] = '0' + u % 10; u /= 10; } while (u);
    if (neg) b[--i] = '-';
    tas_puts(fd, b + i);
}

int main(int argc, char **argv);

#ifdef TAS_MAIN
void *memset(void *d, int c, size_t n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }

__attribute__((used, noreturn)) void tas_cstart(long *sp)
{
    sys_exit(main((int)sp[0], (char **)(sp + 1)));
}
__attribute__((naked, noreturn)) void _start(void)
{
    __asm__ volatile("mov r0, sp\n\tbl tas_cstart\n");
}
#endif

#endif
