#include "layout.h"
#include "util.h"

#include <math.h>

void layout_calc_tile(int n_images, const int *img_w, const int *img_h,
		      int avail_w, int avail_h,
		      struct layout_slot *slots)
{
	int cols, rows;
	int cell_w, cell_h;
	int i;

	if (n_images <= 0)
		return;

	if (n_images == 1) {
		float sx, sy;

		sx = (float)avail_w / img_w[0];
		sy = (float)avail_h / img_h[0];
		slots[0].scale = MIN(sx, sy);
		slots[0].display_w = (int)(img_w[0] * slots[0].scale);
		slots[0].display_h = (int)(img_h[0] * slots[0].scale);
		slots[0].canvas_x = (avail_w - slots[0].display_w) / 2;
		slots[0].canvas_y = 0;
		return;
	}

	cols = (int)ceil(sqrt((double)n_images));
	rows = (n_images + cols - 1) / cols;

	cell_w = avail_w / cols;
	cell_h = avail_h / rows;

	for (i = 0; i < n_images; i++) {
		int row, col;
		float sx, sy;

		row = i / cols;
		col = i % cols;

		sx = (float)cell_w / img_w[i];
		sy = (float)cell_h / img_h[i];
		slots[i].scale = MIN(sx, sy);

		slots[i].display_w = (int)(img_w[i] * slots[i].scale);
		slots[i].display_h = (int)(img_h[i] * slots[i].scale);

		slots[i].canvas_x = col * cell_w +
			(cell_w - slots[i].display_w) / 2;
		slots[i].canvas_y = (row * cell_h +
			(cell_h - slots[i].display_h) / 2) / 2;
	}
}
