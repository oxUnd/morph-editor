#ifndef MORPH_EDITOR_MASK_H
#define MORPH_EDITOR_MASK_H

#include <stdint.h>
#include "bbox.h"
#include "arena.h"

struct mask_options {
	int feather;
	int invert;
};

/*
 * mask_from_bboxes - generate binary mask from bboxes
 * Returns arena-allocated mask buffer (1 byte per pixel, 0 or 255).
 */
unsigned char *mask_from_bboxes(struct arena *a, struct bbox *boxes,
				int count, int img_w, int img_h,
				struct mask_options opts);

/*
 * mask_from_base64 - decode base64 mask data
 * Returns malloc'd buffer. Sets *w, *h from embedded info.
 */
unsigned char *mask_from_base64(const char *b64, int *w, int *h);

/*
 * mask_combine - combine two masks with operation
 * op: 0=OR, 1=AND, 2=XOR, 3=SUBTRACT
 * Returns arena-allocated buffer.
 */
unsigned char *mask_combine(struct arena *a, unsigned char *a_mask,
			    unsigned char *b_mask, int w, int h, int op);

/*
 * mask_feather - apply gaussian-like feather to mask
 * Modifies mask in-place.
 */
void mask_feather(unsigned char *mask, int w, int h, int radius);

/*
 * mask_invert - invert mask values
 * Modifies mask in-place.
 */
void mask_invert(unsigned char *mask, int w, int h);

#endif /* MORPH_EDITOR_MASK_H */
