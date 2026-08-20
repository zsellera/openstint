/*
 * <string.h> for MinGW, layered on top of the real one to add strsep().
 *
 * liquid-dsp's src/core/src/logging.c calls strsep() to parse the logging
 * config string. strsep() is BSD, picked up by glibc and Apple libc, and absent
 * from MinGW-w64. Since GCC 14 an implicit declaration is an error rather than
 * a warning, so this is a hard build failure on Windows and nothing else.
 *
 * The alternative was configuring liquid with ENABLE_LOGGING=OFF on Windows,
 * which compiles because the call sits inside #ifdef LIQUID_LOGGING_ENABLE.
 * That was rejected: it would make the Windows decoder report errors
 * differently from the Linux and macOS ones, which is the per-platform
 * divergence this whole vendoring exercise exists to remove. Ten lines of
 * strsep() is the cheaper answer.
 *
 * #include_next reaches the real MinGW <string.h> underneath, so everything
 * else behaves normally. Reached only via a PRIVATE include directory on
 * liquid's `core` target -- see cmake/Dependencies.cmake. It is not on the
 * include path of OpenStint's own sources.
 */

#ifndef OPENSTINT_MINGW_STRING_H
#define OPENSTINT_MINGW_STRING_H

#include_next <string.h>

/* If a future MinGW-w64 grows its own strsep, this becomes a redefinition
 * error rather than silently shadowing it -- which is the outcome we want. */
static __inline char * strsep(char ** _stringp, const char * _delim)
{
    char * start = *_stringp;
    char * p;

    if (start == NULL)
        return NULL;

    p = strpbrk(start, _delim);
    if (p == NULL) {
        /* No delimiter left: return the remainder and end the iteration. */
        *_stringp = NULL;
    } else {
        *p        = '\0';
        *_stringp = p + 1;
    }
    return start;
}

#endif /* OPENSTINT_MINGW_STRING_H */
