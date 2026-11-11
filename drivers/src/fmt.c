#include "fmt.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char *buf;
    size_t size;
    size_t pos;
} out_t;

static void put(out_t *o, char c)
{
    if (o->pos + 1U < o->size) {
        o->buf[o->pos++] = c;
    }
}

static void put_number(out_t *o, uint32_t value, uint32_t base, bool upper, bool negative, unsigned width, char pad)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[11];
    unsigned n = 0U;
    do {
        tmp[n++] = digits[value % base];
        value /= base;
    } while (value != 0U);

    unsigned len = n + (negative ? 1U : 0U);
    if (negative && (pad == '0')) {
        put(o, '-');
    }
    while (width > len) {
        put(o, pad);
        width--;
    }
    if (negative && (pad != '0')) {
        put(o, '-');
    }
    while (n > 0U) {
        put(o, tmp[--n]);
    }
}

size_t fmt_vsnprintf(char *buf, size_t size, const char *format, va_list ap)
{
    out_t o = {buf, size, 0U};
    if ((buf == NULL) || (size == 0U)) {
        return 0U;
    }
    for (const char *p = format; *p != '\0'; p++) {
        if (*p != '%') {
            put(&o, *p);
            continue;
        }
        p++;
        char pad = ' ';
        unsigned width = 0U;
        if (*p == '0') {
            pad = '0';
            p++;
        }
        while ((*p >= '0') && (*p <= '9')) {
            width = (width * 10U) + (unsigned)(*p - '0');
            p++;
        }
        while (*p == 'l') {  // long is 32-bit on this target: accept and ignore
            p++;
        }
        switch (*p) {
        case '%':
            put(&o, '%');
            break;
        case 'c':
            put(&o, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) {
                s = "(null)";
            }
            while (*s != '\0') {
                put(&o, *s++);
            }
            break;
        }
        case 'd':
        case 'i': {
            int32_t v = (int32_t)va_arg(ap, int);
            uint32_t mag = (v < 0) ? (uint32_t)(-(v + 1)) + 1U : (uint32_t)v;
            put_number(&o, mag, 10U, false, v < 0, width, pad);
            break;
        }
        case 'u':
            put_number(&o, (uint32_t)va_arg(ap, unsigned), 10U, false, false, width, pad);
            break;
        case 'x':
        case 'X':
            put_number(&o, (uint32_t)va_arg(ap, unsigned), 16U, *p == 'X', false, width, pad);
            break;
        case '\0':
            p--;  // format ended right after '%': stop at the terminator on the next iteration
            break;
        default:
            put(&o, '%');
            put(&o, *p);
            break;
        }
    }
    o.buf[o.pos] = '\0';
    return o.pos;
}

size_t fmt_snprintf(char *buf, size_t size, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    size_t n = fmt_vsnprintf(buf, size, format, ap);
    va_end(ap);
    return n;
}
