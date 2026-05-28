#ifndef MORPH_EDITOR_TRANSFORM_H
#define MORPH_EDITOR_TRANSFORM_H

#include <stdint.h>
#include "arena.h"

#define PAD_MODE_SOLID 0
#define PAD_MODE_TRANSPARENT 1

struct transform_options {
	int resize_w, resize_h;
	int rotate;
	int flip_h, flip_v;
	int pad_w, pad_h;
	uint32_t pad_bg;
	int pad_mode;
};

/*
 * transform_apply - apply transform to image
 * Modifies *pixels, *w, *h. Returns 0 on success.
 * New pixel buffer allocated from arena.
 */
int transform_apply(struct arena *a, unsigned char **pixels, int *w, int *h,
		    int channels, struct transform_options opts);

#endif /* MORPH_EDITOR_TRANSFORM_H */
