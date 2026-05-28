#include "canvas.h"
#include "image.h"
#include "util.h"

#include <string.h>
#include <stdlib.h>

unsigned char *canvas_composite(struct arena *a, int canvas_w, int canvas_h,
				struct canvas_layer *layers,
				int layer_count, uint32_t bg_color)
{
	unsigned char *canvas;
	int i, x, y;
	size_t sz;

	sz = (size_t)canvas_w * canvas_h * 4;
	canvas = arena_alloc(a, sz);
	if (!canvas)
		return NULL;

	/* Fill background */
	for (i = 0; i < canvas_w * canvas_h; i++) {
		canvas[i * 4] = COLOR_R(bg_color);
		canvas[i * 4 + 1] = COLOR_G(bg_color);
		canvas[i * 4 + 2] = COLOR_B(bg_color);
		canvas[i * 4 + 3] = 255;
	}

	/* Composite each layer */
	for (i = 0; i < layer_count; i++) {
		struct canvas_layer *layer = &layers[i];
		unsigned char *src;
		int src_w, src_h, src_ch;

		if (layer->pixels) {
			src = layer->pixels;
			src_w = layer->w;
			src_h = layer->h;
			src_ch = 4;
		} else if (layer->image_path) {
			struct image img;

			if (image_load(a, layer->image_path, &img) < 0)
				continue;
			src = img.pixels;
			src_w = img.width;
			src_h = img.height;
			src_ch = img.channels;
		} else {
			continue;
		}

		for (y = 0; y < src_h; y++) {
			int dy = layer->y + y;

			if (dy < 0 || dy >= canvas_h)
				continue;
			for (x = 0; x < src_w; x++) {
				int dx = layer->x + x;
				unsigned char *dst_p;
				unsigned char *src_p;
				float alpha;

				if (dx < 0 || dx >= canvas_w)
					continue;

				src_p = src + (y * src_w + x) * src_ch;
				dst_p = canvas + (dy * canvas_w + dx) * 4;

				alpha = (src_ch == 4) ?
					src_p[3] / 255.0f : 1.0f;
				alpha *= layer->opacity;

				dst_p[0] = (unsigned char)(
					src_p[0] * alpha +
					dst_p[0] * (1 - alpha));
				dst_p[1] = (unsigned char)(
					src_p[1] * alpha +
					dst_p[1] * (1 - alpha));
				dst_p[2] = (unsigned char)(
					src_p[2] * alpha +
					dst_p[2] * (1 - alpha));
			}
		}
	}

	return canvas;
}
