#include "image.h"
#include "json_util.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_PSD
#define STBI_ONLY_GIF
#define STBI_ONLY_HDR

#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int image_load(struct arena *a, const char *path, struct image *img)
{
	unsigned char *data;
	int w, h, channels;

	data = stbi_load(path, &w, &h, &channels, 4);
	if (!data)
		return -1;

	/*
	 * For very large images, use system malloc since arenas
	 * are not ideal for huge single allocations.
	 */
	if ((size_t)w * h * 4 > 64 * 1024 * 1024) {
		img->pixels = data;
	} else {
		img->pixels = arena_alloc(a, (size_t)w * h * 4);
		if (!img->pixels) {
			stbi_image_free(data);
			return -1;
		}
		memcpy(img->pixels, data, (size_t)w * h * 4);
		stbi_image_free(data);
	}

	img->width = w;
	img->height = h;
	img->channels = 4;
	strncpy(img->path, path, sizeof(img->path) - 1);
	img->path[sizeof(img->path) - 1] = '\0';
	return 0;
}

int image_load_from_bytes(struct arena *a, const unsigned char *data,
			  int len, struct image *img)
{
	unsigned char *pixels;
	int w, h, channels;

	pixels = stbi_load_from_memory(data, len, &w, &h, &channels, 4);
	if (!pixels)
		return -1;

	if ((size_t)w * h * 4 > 64 * 1024 * 1024) {
		img->pixels = pixels;
	} else {
		img->pixels = arena_alloc(a, (size_t)w * h * 4);
		if (!img->pixels) {
			stbi_image_free(pixels);
			return -1;
		}
		memcpy(img->pixels, pixels, (size_t)w * h * 4);
		stbi_image_free(pixels);
	}

	img->width = w;
	img->height = h;
	img->channels = 4;
	img->path[0] = '\0';
	return 0;
}

int image_load_base64(struct arena *a, const char *b64, struct image *img)
{
	unsigned char *data;
	int len;
	int ret;

	data = base64_decode(b64, &len);
	if (!data)
		return -1;

	ret = image_load_from_bytes(a, data, len, img);
	free(data);
	return ret;
}

int image_save(const char *path, const unsigned char *pixels,
	       int w, int h, int channels)
{
	const char *ext;

	ext = strrchr(path, '.');
	if (!ext)
		return -1;

	if (strcmp(ext, ".png") == 0)
		return stbi_write_png(path, w, h, channels,
				      pixels, w * channels) ? 0 : -1;
	if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
		return stbi_write_jpg(path, w, h, channels,
				      pixels, 90) ? 0 : -1;
	if (strcmp(ext, ".bmp") == 0)
		return stbi_write_bmp(path, w, h, channels, pixels) ? 0 : -1;
	if (strcmp(ext, ".tga") == 0)
		return stbi_write_tga(path, w, h, channels, pixels) ? 0 : -1;

	return -1;
}

static void write_buf_callback(void *context, void *data, int size)
{
	struct {
		unsigned char *buf;
		int len;
		int cap;
	} *ctx = context;
	unsigned char *new_buf;

	if (ctx->len + size > ctx->cap) {
		ctx->cap = (ctx->len + size) * 2;
		new_buf = realloc(ctx->buf, ctx->cap);
		if (!new_buf)
			return;
		ctx->buf = new_buf;
	}
	memcpy(ctx->buf + ctx->len, data, size);
	ctx->len += size;
}

unsigned char *image_save_png_to_buf(const unsigned char *pixels,
				     int w, int h, int channels,
				     int *out_len)
{
	struct {
		unsigned char *buf;
		int len;
		int cap;
	} ctx;

	ctx.buf = malloc(w * h * channels + 4096);
	ctx.len = 0;
	ctx.cap = w * h * channels + 4096;
	if (!ctx.buf)
		return NULL;

	stbi_write_png_to_func(write_buf_callback, &ctx,
			       w, h, channels, pixels, w * channels);

	*out_len = ctx.len;
	return ctx.buf;
}

unsigned char *image_resize(struct arena *a, const unsigned char *pixels,
			    int w, int h, int channels,
			    int new_w, int new_h)
{
	unsigned char *out;
	size_t sz;

	sz = (size_t)new_w * new_h * channels;
	if (sz > 64 * 1024 * 1024)
		out = malloc(sz);
	else
		out = arena_alloc(a, sz);
	if (!out)
		return NULL;

	/*
	 * Use sRGB-aware resizing with the MITCHELL filter so that
	 * heavy downscaling (e.g. many images laid out in a grid)
	 * stays sharp instead of looking washed-out / blurry.
	 * stbir_resize_uint8_linear treats pixels as linear and uses
	 * the default filter which produces noticeably soft results
	 * when the scale ratio is small.
	 */
	stbir_resize(pixels, w, h, w * channels,
		     out, new_w, new_h, new_w * channels,
		     (stbir_pixel_layout)channels,
		     STBIR_TYPE_UINT8_SRGB,
		     STBIR_EDGE_CLAMP,
		     STBIR_FILTER_MITCHELL);
	return out;
}

void image_free(struct image *img)
{
	/*
	 * If the image was too large for arena and was
	 * allocated via system malloc, free it.
	 * Otherwise arena handles cleanup.
	 */
	if (img->pixels && img->path[0]) {
		if ((size_t)img->width * img->height * 4 >
		    64 * 1024 * 1024)
			free(img->pixels);
	}
	img->pixels = NULL;
	img->width = 0;
	img->height = 0;
}

char *image_info_json(const char *path)
{
	unsigned char *data;
	int w, h, channels;
	cJSON *root;
	char *result;

	data = stbi_load(path, &w, &h, &channels, 0);
	if (!data)
		return NULL;
	stbi_image_free(data);

	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "path", path);
	cJSON_AddNumberToObject(root, "width", w);
	cJSON_AddNumberToObject(root, "height", h);
	cJSON_AddNumberToObject(root, "channels", channels);

	result = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return result;
}
