/* newlib system-call layer for running arm-none-eabi binaries directly on ARM Linux.
 * Provides _start, the newlib stubs (_open, _read, _sbrk, ...) on top of Linux syscalls,
 * and translates newlib's open() flags to Linux values. */
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include "lsys.h"

#undef errno
extern int errno;
char **environ;
static char *empty_env[] = { 0 };

static int ret(long r) { if (r < 0 && r > -4096) { errno = (int)-r; return -1; } return (int)r; }

int main(int argc, char **argv);

__attribute__((used, noreturn)) void newlib_cstart(long *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    environ = argv + argc + 1;
    if (!environ) environ = empty_env;
    exit(main(argc, argv));
}
__attribute__((naked, noreturn)) void _start(void)
{
    __asm__ volatile("mov r0, sp\n\tbl newlib_cstart\n");
}

void _exit(int code) { for (;;) LS3(LNR_exit_group, code, 0, 0); }
void _init(void) {}     /* normally from crti.o; we link with -nostartfiles */
void _fini(void) {}

int _open(const char *path, int flags, int mode)
{
    int lf = 0;
    switch (flags & O_ACCMODE) { case O_WRONLY: lf = L_O_WRONLY; break; case O_RDWR: lf = L_O_RDWR; break; default: break; }
    if (flags & O_CREAT)  lf |= L_O_CREAT;
    if (flags & O_TRUNC)  lf |= L_O_TRUNC;
    if (flags & O_APPEND) lf |= L_O_APPEND;
    if (flags & O_EXCL)   lf |= L_O_EXCL;
    return ret(LS3(LNR_open, path, lf, mode ? mode : 0644));
}
int _close(int fd) { return ret(LS3(LNR_close, fd, 0, 0)); }
int _read(int fd, char *b, int n) { return ret(LS3(LNR_read, fd, b, n)); }
int _write(int fd, const char *b, int n) { return ret(LS3(LNR_write, fd, b, n)); }
int _lseek(int fd, int off, int whence) { return ret(LS3(LNR_lseek, fd, off, whence)); }
int _isatty(int fd) { return fd <= 2; }
int _getpid(void) { return (int)LS3(LNR_getpid, 0, 0, 0); }
int _kill(int pid, int sig) { return ret(LS3(LNR_kill, pid, sig, 0)); }
int _unlink(const char *p) { return ret(LS3(LNR_unlink, p, 0, 0)); }
int _link(const char *a, const char *b) { (void)a; (void)b; errno = EMLINK; return -1; }
int _rename(const char *a, const char *b) { return ret(LS3(LNR_rename, a, b, 0)); }
int mkdir(const char *p, mode_t m) { return ret(LS3(LNR_mkdir, p, m, 0)); }
clock_t _times(struct tms *t) { (void)t; return (clock_t)lms(); }
int _gettimeofday(struct timeval *tv, void *tz) { (void)tz; return ret(LS3(LNR_gettimeofday, tv, 0, 0)); }

static void fill_stat(struct stat *st, struct lstat64 *ls)
{
    st->st_mode = ls->st_mode;
    st->st_size = (off_t)ls->st_size;
    st->st_nlink = ls->st_nlink;
    st->st_blksize = 4096;
}
int _fstat(int fd, struct stat *st)
{
    struct lstat64 ls;
    int r = ret(LS3(LNR_fstat64, fd, &ls, 0));
    if (!r) fill_stat(st, &ls);
    return r;
}
int _stat(const char *p, struct stat *st)
{
    struct lstat64 ls;
    int r = ret(LS3(LNR_stat64, p, &ls, 0));
    if (!r) fill_stat(st, &ls);
    return r;
}

void *_sbrk(ptrdiff_t inc)
{
    static unsigned long cur;
    if (!cur) cur = (unsigned long)LS3(LNR_brk, 0, 0, 0);
    unsigned long old = cur, want = cur + inc;
    unsigned long got = (unsigned long)LS3(LNR_brk, want, 0, 0);
    if (got < want) { errno = ENOMEM; return (void *)-1; }
    cur = want;
    return (void *)old;
}
