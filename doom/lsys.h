/* Raw Linux/ARM EABI syscalls for the newlib-based Doom port (no libc types mixed in). */
#ifndef LSYS_H
#define LSYS_H

static inline long lsys(long n, long a, long b, long c, long d, long e, long f)
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
#define LS3(n, a, b, c) lsys(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)

#define LNR_exit_group 248
#define LNR_read        3
#define LNR_write       4
#define LNR_open        5
#define LNR_close       6
#define LNR_unlink     10
#define LNR_getpid     20
#define LNR_lseek      19
#define LNR_kill       37
#define LNR_rename     38
#define LNR_mkdir      39
#define LNR_brk        45
#define LNR_ioctl      54
#define LNR_gettimeofday 78
#define LNR_poll      168
#define LNR_nanosleep 162
#define LNR_mmap2     192
#define LNR_stat64    195
#define LNR_fstat64   197
#define LNR_clock_gettime 263

/* Linux open flags (differ from newlib's) */
#define L_O_WRONLY   01
#define L_O_RDWR     02
#define L_O_CREAT    0100
#define L_O_EXCL     0200
#define L_O_TRUNC    01000
#define L_O_APPEND   02000
#define L_O_NONBLOCK 04000

struct lstat64 {   /* ARM EABI struct stat64 */
    unsigned long long st_dev; unsigned char pad0[4]; unsigned int st_ino_lo;
    unsigned int st_mode, st_nlink, st_uid, st_gid;
    unsigned long long st_rdev; unsigned char pad3[4];
    long long st_size;
    unsigned int st_blksize, pad4;
    unsigned long long st_blocks;
    unsigned int a_time, a_time_ns, m_time, m_time_ns, c_time, c_time_ns;   /* st_atime is a newlib macro */
    unsigned long long st_ino;
};

static inline unsigned int lms(void)
{
    long ts[2] = { 0, 0 };
    LS3(LNR_clock_gettime, 1, ts, 0);
    return (unsigned int)(ts[0] * 1000 + ts[1] / 1000000);
}

#endif
