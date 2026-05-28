#include "mask.h"
#include "json_util.h"
#include "image.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

unsigned char *mask_from_bboxes(struct arena *a, struct bbox *boxes,
				int count, int img_w, int img_h,
				struct mask_options opts)
{
	unsigned char *mask;
	int i, x, y;
	struct bbox *b;

	mask = arena_alloc(a, (size_t)img_w * img_h);
	if (!mask)
		return NULL;
	memset(mask, 0, (size_t)img_w * img_h);

	for (i = 0; i < count; i++) {
		b = &boxes[i];
		for (y = MAX(b->y, 0); y < MIN(b->y + b->h, img_h); y++) {
			for (x = MAX(b->x, 0); x < MIN(b->x + b->w, img_w);
			     x++) {
				mask[y * img_w + x] = 255;
			}
		}
	}

	if (opts.feather > 0)
		mask_feather(mask, img_w, img_h, opts.feather);
	if (opts.invert)
		mask_invert(mask, img_w, img_h);

	return mask;
}

unsigned char *mask_from_base64(const char *b64, int *w, int *h)
{
	/*
	 * Decode base64 mask data. Expects a grayscale or RGBA
	 * image encoded as base64 PNG.
	 */
	struct arena a;
	struct image img;
	unsigned char *result;
	int ret;

	arena_init(&a);
	ret = image_load_base64(&a, b64, &img);
	if (ret < 0) {
		arena_free(&a);
		return NULL;
	}

	*w = img.width;
	*h = img.height;
	result = malloc((size_t)(*w) * (*h));
	if (!result) {
		arena_free(&a);
		return NULL;
	}

	/* Take first channel as mask */
	{
		int i;

		for (i = 0; i < (*w) * (*h); i++)
			result[i] = img.pixels[i * 4];
	}

	arena_free(&a);
	return result;
}

unsigned char *mask_combine(struct arena *a, unsigned char *a_mask,
			    unsigned char *b_mask, int w, int h, int op)
{
	unsigned char *result;
	int i, sz;

	sz = w * h;
	result = arena_alloc(a, sz);
	if (!result)
		return NULL;

	for (i = 0; i < sz; i++) {
		switch (op) {
		case 0: /* OR */
			result[i] = a_mask[i] | b_mask[i];
			break;
		case 1: /* AND */
			result[i] = a_mask[i] & b_mask[i];
			break;
		case 2: /* XOR */
			result[i] = a_mask[i] ^ b_mask[i];
			break;
		case 3: /* SUBTRACT */
			result[i] = (a_mask[i] > b_mask[i]) ?
				    a_mask[i] - b_mask[i] : 0;
			break;
		default:
			result[i] = a_mask[i];
			break;
		}
	}
	return result;
}

void mask_feather(unsigned char *mask, int w, int h, int radius)
{
	unsigned char *tmp;
	int x, y, dx, dy;
	int sum, count;

	tmp = malloc((size_t)w * h);
	if (!tmp)
		return;
	memcpy(tmp, mask, (size_t)w * h);

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			sum = 0;
			count = 0;
			for (dy = -radius; dy <= radius; dy++) {
				for (dx = -radius; dx <= radius; dx++) {
					int nx = x + dx;
					int ny = y + dy;

					if (nx >= 0 && nx < w &&
					    ny >= 0 && ny < h) {
						sum += tmp[ny * w + nx];
						count++;
					}
				}
			}
			mask[y * w + x] = (unsigned char)(sum / count);
		}
	}

	free(tmp);
}

void mask_invert(unsigned char *mask, int w, int h)
{
	int i, sz;

	sz = w * h;
	for (i = 0; i < sz; i++)
		mask[i] = 255 - mask[i];
}
