#include "transform.h"
#include "image.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

int transform_apply(struct arena *a, unsigned char **pixels, int *w, int *h,
		    int channels, struct transform_options opts)
{
	unsigned char *src;
	unsigned char *dst;
	int src_w, src_h;

	src = *pixels;
	src_w = *w;
	src_h = *h;

	/* Resize */
	if (opts.resize_w > 0 && opts.resize_h > 0) {
		dst = image_resize(a, src, src_w, src_h, channels,
				   opts.resize_w, opts.resize_h);
		if (!dst)
			return -1;
		src = dst;
		src_w = opts.resize_w;
		src_h = opts.resize_h;
	}

	/* Rotate (90 degree increments) */
	if (opts.rotate != 0) {
		int angle = ((opts.rotate % 360) + 360) % 360;
		int rotations = angle / 90;
		int r;

		for (r = 0; r < rotations; r++) {
			int new_w = src_h;
			int new_h = src_w;
			int x, y;

			dst = arena_alloc(a, (size_t)new_w * new_h *
					  channels);
			if (!dst)
				return -1;

			for (y = 0; y < new_h; y++) {
				for (x = 0; x < new_w; x++) {
					unsigned char *sp;
					unsigned char *dp;

					sp = src + (x * src_w +
						    (src_w - 1 - y)) *
					     channels;
					dp = dst + (y * new_w + x) *
					     channels;
					memcpy(dp, sp, channels);
				}
			}
			src = dst;
			src_w = new_w;
			src_h = new_h;
		}
	}

	/* Flip horizontal */
	if (opts.flip_h) {
		int y, x;

		dst = arena_alloc(a, (size_t)src_w * src_h * channels);
		if (!dst)
			return -1;
		for (y = 0; y < src_h; y++) {
			for (x = 0; x < src_w; x++) {
				memcpy(dst + (y * src_w + x) * channels,
				       src + (y * src_w +
					      (src_w - 1 - x)) * channels,
				       channels);
			}
		}
		src = dst;
	}

	/* Flip vertical */
	if (opts.flip_v) {
		int y;

		dst = arena_alloc(a, (size_t)src_w * src_h * channels);
		if (!dst)
			return -1;
		for (y = 0; y < src_h; y++) {
			memcpy(dst + y * src_w * channels,
			       src + (src_h - 1 - y) * src_w * channels,
			       src_w * channels);
		}
		src = dst;
	}

	/* Pad */
	if (opts.pad_w > src_w || opts.pad_h > src_h) {
		int pw, ph;
		int y;

		pw = MAX(opts.pad_w, src_w);
		ph = MAX(opts.pad_h, src_h);

		dst = arena_alloc(a, (size_t)pw * ph * channels);
		if (!dst)
			return -1;

		/* Fill with pad color */
		if (opts.pad_mode == PAD_MODE_TRANSPARENT && channels == 4) {
			memset(dst, 0, (size_t)pw * ph * channels);
		} else {
			int i;
			for (i = 0; i < pw * ph; i++) {
				dst[i * channels] = COLOR_R(opts.pad_bg);
				dst[i * channels + 1] = COLOR_G(opts.pad_bg);
				dst[i * channels + 2] = COLOR_B(opts.pad_bg);
				if (channels == 4)
					dst[i * channels + 3] = 255;
			}
		}

		/* Copy source centered */
		{
			int ox = (pw - src_w) / 2;
			int oy = (ph - src_h) / 2;

			for (y = 0; y < src_h; y++) {
				int dy = oy + y;

				if (dy < 0 || dy >= ph)
					continue;
				memcpy(dst + (dy * pw + ox) * channels,
				       src + y * src_w * channels,
				       src_w * channels);
			}
		}
		src = dst;
		src_w = pw;
		src_h = ph;
	}

	*pixels = src;
	*w = src_w;
	*h = src_h;
	return 0;
}
