#ifndef MORPH_EDITOR_LAYOUT_H
#define MORPH_EDITOR_LAYOUT_H

#define MAX_IMAGES 16

struct layout_slot {
	int canvas_x, canvas_y;
	int display_w, display_h;
	float scale;
};

/*
 * layout_calc_tile - compute auto-tile grid layout for multiple images.
 * Each image is scaled to fit inside its grid cell while preserving
 * aspect ratio. Images are centered within their cell.
 */
void layout_calc_tile(int n_images, const int *img_w, const int *img_h,
		      int avail_w, int avail_h,
		      struct layout_slot *slots);

#endif /* MORPH_EDITOR_LAYOUT_H */
