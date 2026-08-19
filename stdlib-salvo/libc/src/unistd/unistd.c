/* salvo-libc <unistd.h>/<fcntl.h> wrappers over raw Linux syscalls. */

#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include "syscall.h"

ssize_t read(int fd, void *buf, size_t count)
{
    return (ssize_t)__salvo_syscall_ret(
        __syscall3(__NR_read, (long)fd, (long)(intptr_t)buf, (long)count));
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return (ssize_t)__salvo_syscall_ret(
        __syscall3(__NR_write, (long)fd, (long)(intptr_t)buf, (long)count));
}

int close(int fd)
{
    return (int)__salvo_syscall_ret(__syscall1(__NR_close, (long)fd));
}

off_t lseek(int fd, off_t offset, int whence)
{
    return (off_t)__salvo_syscall_ret(
        __syscall3(__NR_lseek, (long)fd, (long)offset, (long)whence));
}

int open(const char *path, int flags, ...)
{
    mode_t mode = 0;

    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
#if defined(__NR_open)
    return (int)__salvo_syscall_ret(
        __syscall3(__NR_open, (long)(intptr_t)path, (long)flags,
                   (long)mode));
#else
    /* asm-generic architectures (aarch64, riscv64) only have openat. */
    return (int)__salvo_syscall_ret(
        __syscall4(__NR_openat, (long)AT_FDCWD, (long)(intptr_t)path,
                   (long)flags, (long)mode));
#endif
}

void _exit(int status)
{
    __syscall1(__NR_exit_group, (long)status);
    for (;;) { /* the kernel never lets this return */ }
}

pid_t getpid(void)
{
    return (pid_t)__syscall0(__NR_getpid);
}

int brk(void *addr)
{
    long ret = __syscall1(__NR_brk, (long)(intptr_t)addr);
    /* brk(2) reports failure by returning the unchanged break. */
    if (ret != (long)(intptr_t)addr) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void *sbrk(ptrdiff_t increment)
{
    static char *current;
    char *requested;
    char *previous;
    long ret;

    if (current == NULL)
        current = (char *)(intptr_t)__syscall1(__NR_brk, 0);
    if (increment == 0)
        return current;
    requested = current + increment;
    ret = __syscall1(__NR_brk, (long)(intptr_t)requested);
    if ((char *)(intptr_t)ret != requested) {
        errno = ENOMEM;
        return (void *)-1;
    }
    previous = current;
    current = requested;
    return previous;
}
