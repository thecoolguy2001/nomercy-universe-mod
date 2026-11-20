/* The compiler may emit calls to these for struct copies; no libc on N64.
 * Built with -fno-builtin so these loops are not turned into calls to
 * themselves. */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    volatile unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    volatile unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    volatile unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
