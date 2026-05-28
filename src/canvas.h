#ifndef MORPH_EDITOR_CANVAS_H
#define MORPH_EDITOR_CANVAS_H

#include <stdint.h>
#include "arena.h"

enum blend_mode {
	BLEND_NORMAL,
	BLEND_MULTIPLY,
	BLEND_SCREEN,
	BLEND_OVERLAY
};

struct canvas_layer {
	char *image_path;
	unsigned char *pixels;
	int w, h;
	int x, y;
	float opacity;
	enum blend_mode blend;
};

/*
 * canvas_composite - composite multiple layers onto a canvas
 * Returns arena-allocated pixel buffer (RGBA).
 */
unsigned char *canvas_composite(struct arena *a, int canvas_w, int canvas_h,
				struct canvas_layer *layers,
				int layer_count, uint32_t bg_color);

#endif /* MORPH_EDITOR_CANVAS_H */
