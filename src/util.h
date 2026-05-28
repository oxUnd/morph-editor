#ifndef MORPH_EDITOR_UTIL_H
#define MORPH_EDITOR_UTIL_H

#include <stdint.h>
#include <stddef.h>
#include "arena.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define COLOR_R(c) (((c) >> 16) & 0xff)
#define COLOR_G(c) (((c) >> 8) & 0xff)
#define COLOR_B(c) ((c) & 0xff)
#define COLOR_RGB(r, g, b) (((uint32_t)(r) << 16) | \
			    ((uint32_t)(g) << 8) | (uint32_t)(b))

/*
 * str_copy - copy string into arena-allocated buffer
 */
char *str_copy(struct arena *a, const char *src, size_t len);

/*
 * file_exists - check if a file exists and is readable
 */
int file_exists(const char *path);

/*
 * file_size - get file size in bytes, -1 on error
 */
long file_size(const char *path);

/*
 * log_verbose - global verbose flag
 */
extern int g_verbose;

/*
 * morph_log - log a message (to stderr or log file)
 */
void morph_log(const char *fmt, ...);

/*
 * morph_vlog - log a message (va_list variant)
 */
void morph_vlog(const char *fmt, __builtin_va_list ap);

/*
 * log_set_file - redirect logs to a file
 */
void log_set_file(const char *path);

/*
 * log_close - close log file if open
 */
void log_close(void);

#endif /* MORPH_EDITOR_UTIL_H */
