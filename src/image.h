#ifndef MORPH_EDITOR_IMAGE_H
#define MORPH_EDITOR_IMAGE_H

#include <stdint.h>
#include "arena.h"

struct image {
	unsigned char *pixels;
	int width;
	int height;
	int channels;
	char path[512];
};

/*
 * image_load - load image from file path
 * Returns 0 on success, negative on error.
 */
int image_load(struct arena *a, const char *path, struct image *img);

/*
 * image_load_base64 - load image from base64-encoded data
 * Returns 0 on success, negative on error.
 */
int image_load_base64(struct arena *a, const char *b64, struct image *img);

/*
 * image_load_from_bytes - load image from raw bytes in memory
 * Returns 0 on success, negative on error.
 */
int image_load_from_bytes(struct arena *a, const unsigned char *data,
			  int len, struct image *img);

/*
 * image_save - save image to file (format from extension)
 * Returns 0 on success, negative on error.
 */
int image_save(const char *path, const unsigned char *pixels,
	       int w, int h, int channels);

/*
 * image_save_png_to_buf - encode image as PNG to malloc'd buffer
 * Returns buffer, sets *out_len. Caller must free.
 */
unsigned char *image_save_png_to_buf(const unsigned char *pixels,
				     int w, int h, int channels,
				     int *out_len);

/*
 * image_resize - resize image to new dimensions
 * Returns new pixel buffer allocated from arena.
 */
unsigned char *image_resize(struct arena *a, const unsigned char *pixels,
			    int w, int h, int channels,
			    int new_w, int new_h);

/*
 * image_free - free image resources
 */
void image_free(struct image *img);

/*
 * image_info_json - get image metadata as JSON string
 * Returns malloc'd string, caller must free.
 */
char *image_info_json(const char *path);

#endif /* MORPH_EDITOR_IMAGE_H */
