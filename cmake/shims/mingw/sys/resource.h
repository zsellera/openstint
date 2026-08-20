/*
 * Minimal <sys/resource.h> for MinGW, so liquid-dsp's src/core/src/timer.c
 * compiles on Windows.
 *
 * timer.c includes this header unconditionally and calls getrusage() to
 * implement LIQUID_TIMER_RUSAGE. MinGW-w64 ships no such header -- getrusage()
 * is POSIX, and the Win32 equivalent is GetProcessTimes(). Nothing in OpenStint
 * calls liquid_timer_*; only liquid's own autotests and sandbox do, and both are
 * off. But timer.c is in the `core` object library unconditionally, so the file
 * still has to build.
 *
 * This is deliberately partial: struct rusage carries only the two fields
 * timer.c reads. Filling in ru_maxrss and friends with zeros would look
 * complete while quietly lying. Anything reaching for another field gets a
 * compile error, which is the outcome we want.
 *
 * Reached only via a PRIVATE include directory on liquid's `core` target -- see
 * cmake/Dependencies.cmake. It is not on the include path of OpenStint's own
 * sources.
 */

#ifndef OPENSTINT_MINGW_SYS_RESOURCE_H
#define OPENSTINT_MINGW_SYS_RESOURCE_H

#include <errno.h>
#include <sys/time.h> /* struct timeval; MinGW does provide this one */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)

struct rusage {
    struct timeval ru_utime; /* user CPU time consumed */
    struct timeval ru_stime; /* system CPU time consumed */
};

static __inline int getrusage(int _who, struct rusage * _usage)
{
    FILETIME       creation, exit, kernel, user;
    ULARGE_INTEGER k, u;

    if (_usage == NULL) {
        errno = EFAULT;
        return -1;
    }

    /* Zero before anything can fail. timer.c discards the return value and
     * reads the struct regardless, so leaving it untouched on an error path
     * means reading uninitialised stack -- and because this function is inline,
     * the compiler sees that and warns. Zeros are also the honest answer: the
     * caller that does check still gets -1. */
    _usage->ru_utime.tv_sec = _usage->ru_utime.tv_usec = 0;
    _usage->ru_stime.tv_sec = _usage->ru_stime.tv_usec = 0;

    /* RUSAGE_CHILDREN has no Win32 analogue: a process cannot enumerate the CPU
     * time of children it did not keep handles to. */
    if (_who != RUSAGE_SELF) {
        errno = EINVAL;
        return -1;
    }

    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        errno = EINVAL;
        return -1;
    }

    /* The kernel and user FILETIMEs are durations, not wall-clock instants, and
     * are counted in 100-nanosecond ticks. */
    k.LowPart  = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart  = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;

    _usage->ru_utime.tv_sec  = (time_t)(u.QuadPart / 10000000ULL);
    _usage->ru_utime.tv_usec = (long)  ((u.QuadPart % 10000000ULL) / 10ULL);
    _usage->ru_stime.tv_sec  = (time_t)(k.QuadPart / 10000000ULL);
    _usage->ru_stime.tv_usec = (long)  ((k.QuadPart % 10000000ULL) / 10ULL);
    return 0;
}

#endif /* OPENSTINT_MINGW_SYS_RESOURCE_H */
