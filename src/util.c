#include "util.h"
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

int g_verbose;

static FILE *g_log_fp;

char *str_copy(struct arena *a, const char *src, size_t len)
{
	char *dst;

	dst = arena_alloc(a, len + 1);
	if (!dst)
		return NULL;
	memcpy(dst, src, len);
	dst[len] = '\0';
	return dst;
}

int file_exists(const char *path)
{
	return access(path, R_OK) == 0;
}

long file_size(const char *path)
{
	FILE *fp;
	long sz;

	fp = fopen(path, "rb");
	if (!fp)
		return -1;
	fseek(fp, 0, SEEK_END);
	sz = ftell(fp);
	fclose(fp);
	return sz;
}

void morph_vlog(const char *fmt, va_list ap)
{
	FILE *out;

	out = g_log_fp ? g_log_fp : stderr;
	vfprintf(out, fmt, ap);
	fflush(out);
}

void morph_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	morph_vlog(fmt, ap);
	va_end(ap);
}

void log_set_file(const char *path)
{
	if (g_log_fp)
		fclose(g_log_fp);
	g_log_fp = fopen(path, "a");
}

void log_close(void)
{
	if (g_log_fp) {
		fclose(g_log_fp);
		g_log_fp = NULL;
	}
}
