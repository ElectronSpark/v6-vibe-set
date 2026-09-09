#include <stddef.h>
#include <stdint.h>

static void *xv6_copy_forward(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

static void *xv6_move(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    uintptr_t da = (uintptr_t)d;
    uintptr_t sa = (uintptr_t)s;

    if (d == s || n == 0)
        return dst;
    if (da < sa || da - sa >= n)
        return xv6_copy_forward(dst, src, n);
    for (size_t i = n; i > 0; i--)
        d[i - 1] = s[i - 1];
    return dst;
}

void *xv6_memcpy_glibc_2_14(void *dst, const void *src, size_t n)
{
    return xv6_copy_forward(dst, src, n);
}

void *xv6_memcpy_glibc_2_2_5(void *dst, const void *src, size_t n)
{
    return xv6_copy_forward(dst, src, n);
}

void *xv6_memmove_glibc_2_2_5(void *dst, const void *src, size_t n)
{
    return xv6_move(dst, src, n);
}

static double xv6_floor_scalar(double x)
{
    union {
        double d;
        uint64_t u;
    } v = { .d = x };
    uint64_t sign = v.u & (UINT64_C(1) << 63);
    uint64_t mag = v.u & ~(UINT64_C(1) << 63);
    unsigned int exp = (unsigned int)((v.u >> 52) & 0x7ff);
    int e = (int)exp - 1023;
    uint64_t mask;

    if (exp == 0x7ff)
        return x + x;
    if (e < 0) {
        if (mag == 0)
            return x;
        return sign ? -1.0 : 0.0;
    }
    if (e >= 52)
        return x;

    mask = (UINT64_C(1) << (52 - e)) - 1;
    if ((mag & mask) == 0)
        return x;
    if (sign)
        mag += UINT64_C(1) << (52 - e);
    mag &= ~mask;
    v.u = sign | mag;
    return v.d;
}

static double xv6_ceil_scalar(double x)
{
    union {
        double d;
        uint64_t u;
    } v = { .d = x };
    uint64_t sign = v.u & (UINT64_C(1) << 63);
    uint64_t mag = v.u & ~(UINT64_C(1) << 63);
    unsigned int exp = (unsigned int)((v.u >> 52) & 0x7ff);
    int e = (int)exp - 1023;
    uint64_t mask;

    if (exp == 0x7ff)
        return x + x;
    if (e < 0) {
        if (mag == 0)
            return x;
        return sign ? -0.0 : 1.0;
    }
    if (e >= 52)
        return x;

    mask = (UINT64_C(1) << (52 - e)) - 1;
    if ((mag & mask) == 0)
        return x;
    if (!sign)
        mag += UINT64_C(1) << (52 - e);
    mag &= ~mask;
    v.u = sign | mag;
    return v.d;
}

double xv6_floor_glibc_2_2_5(double x)
{
    return xv6_floor_scalar(x);
}

double xv6_ceil_glibc_2_2_5(double x)
{
    return xv6_ceil_scalar(x);
}

__asm__(".symver xv6_memcpy_glibc_2_14, memcpy@@GLIBC_2.14");
__asm__(".symver xv6_memcpy_glibc_2_2_5, memcpy@GLIBC_2.2.5");
__asm__(".symver xv6_memmove_glibc_2_2_5, memmove@@GLIBC_2.2.5");
__asm__(".symver xv6_floor_glibc_2_2_5, floor@@GLIBC_2.2.5");
__asm__(".symver xv6_ceil_glibc_2_2_5, ceil@@GLIBC_2.2.5");
