/*
 * sysstubs.c — minimal newlib syscall stubs for freestanding PRX.
 *
 * PRX plugins are built with -nostartfiles (no crt0), so newlib's
 * abort() (pulled in via stdio error paths like snprintf) can't find
 * _exit/_kill/_getpid. Provide the standard minimal set. These are
 * never called in normal operation — the plugin has no console I/O.
 *
 * _sbrk backs malloc over a fixed static arena (needed by the CSO
 * reader's transient block buffers).
 */

#include <sys/types.h>

static char heap_arena[512 * 1024];
static char *heap_cur = heap_arena;
static char *heap_end = heap_arena + sizeof(heap_arena);

void _exit(int status)
{
    (void)status;
    for (;;) { }
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

void *_sbrk(ptrdiff_t incr)
{
    char *prev = heap_cur;
    if (incr > (heap_end - heap_cur)) return (void *)-1;
    heap_cur += incr;
    return prev;
}

int _write(int fd, const void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    return (int)count; /* swallow output */
}

int _read(int fd, void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    return -1;
}

int _lseek(int fd, off_t pos, int whence)
{
    (void)fd;
    (void)pos;
    (void)whence;
    return -1;
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _fstat(int fd, void *st)
{
    (void)fd;
    (void)st;
    return -1;
}

int _isatty(int fd)
{
    (void)fd;
    return 0;
}