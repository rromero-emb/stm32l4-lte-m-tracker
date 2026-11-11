#ifndef FMT_H
#define FMT_H

#include <stdarg.h>
#include <stddef.h>

// Minimal snprintf (no floats, no heap, ~1 KB of code instead of newlib's printf).
// Supports %% %c %s %d %i %u %x %X with an optional '0' flag and width, e.g. "%08x".
// Always NUL-terminates when size > 0. Returns the number of characters written (truncated).
size_t fmt_vsnprintf(char *buf, size_t size, const char *format, va_list ap);
size_t fmt_snprintf(char *buf, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));

#endif
