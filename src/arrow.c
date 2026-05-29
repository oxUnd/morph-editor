#include "arrow.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static const uint32_t arrow_colors[] = {
	0xff8800, 0x00ccff, 0xff00ff, 0x88ff00,
	0xff4444, 0x44ff88, 0x8844ff, 0xffff44
};

void arrow_manager_init(struct arrow_manager *am)
{
	memset(am, 0, sizeof(*am));
	am->selected = -1;
	am->next_id = 1;
}

int arrow_add(struct arrow_manager *am, struct arrow_point from,
	      struct arrow_point to, const char *label, uint32_t color)
{
	struct arrow *a;

	if (am->count >= ARROW_MAX)
		return -1;

	a = &am->arrows[am->count];
	memset(a, 0, sizeof(*a));
	a->from = from;
	a->to = to;
	a->id = am->next_id++;
	a->color = color ? color :
		  arrow_colors[am->count % ARRAY_SIZE(arrow_colors)];
	if (label)
		strncpy(a->label, label, ARROW_LABEL_MAX - 1);

	am->count++;
	am->selected = am->count - 1;
	return a->id;
}

int arrow_remove(struct arrow_manager *am, int id)
{
	int i;

	for (i = 0; i < am->count; i++) {
		if (am->arrows[i].id == id) {
			memmove(&am->arrows[i], &am->arrows[i + 1],
				(am->count - i - 1) * sizeof(struct arrow));
			am->count--;
			if (am->selected >= am->count)
				am->selected = am->count - 1;
			return 0;
		}
	}
	return -1;
}

struct arrow *arrow_get_selected(struct arrow_manager *am)
{
	if (am->selected < 0 || am->selected >= am->count)
		return NULL;
	return &am->arrows[am->selected];
}

void arrow_select_next(struct arrow_manager *am)
{
	if (am->count == 0)
		return;
	am->selected = (am->selected + 1) % am->count;
}

static float point_to_segment_dist(float px, float py,
				   float x0, float y0,
				   float x1, float y1)
{
	float dx, dy, t, proj_x, proj_y;

	dx = x1 - x0;
	dy = y1 - y0;

	if (dx == 0.0f && dy == 0.0f) {
		float ddx = px - x0;
		float ddy = py - y0;

		return sqrtf(ddx * ddx + ddy * ddy);
	}

	t = ((px - x0) * dx + (py - y0) * dy) / (dx * dx + dy * dy);
	if (t < 0.0f)
		t = 0.0f;
	else if (t > 1.0f)
		t = 1.0f;

	proj_x = x0 + t * dx;
	proj_y = y0 + t * dy;

	{
		float ddx = px - proj_x;
		float ddy = py - proj_y;

		return sqrtf(ddx * ddx + ddy * ddy);
	}
}

static void arrow_point_to_term(struct arrow_point *pt,
				int *canvas_offsets_x, int *canvas_offsets_y,
				float *scales, int n_images,
				int *tx, int *ty)
{
	int idx = pt->image_index;

	if (idx < 0 || idx >= n_images)
		return;

	*tx = (int)(pt->x * scales[idx]) + canvas_offsets_x[idx];
	*ty = (int)(pt->y * scales[idx] / 2) + canvas_offsets_y[idx];
}

int arrow_find_near(struct arrow_manager *am, int tx, int ty,
		    int *canvas_offsets_x, int *canvas_offsets_y,
		    float *scales, int n_images, int threshold)
{
	int i;
	float best_dist = 1e9f;
	int best_idx = -1;

	for (i = 0; i < am->count; i++) {
		struct arrow *a = &am->arrows[i];
		int from_tx, from_ty, to_tx, to_ty;
		float dist;

		arrow_point_to_term(&a->from, canvas_offsets_x,
				    canvas_offsets_y, scales, n_images,
				    &from_tx, &from_ty);
		arrow_point_to_term(&a->to, canvas_offsets_x,
				    canvas_offsets_y, scales, n_images,
				    &to_tx, &to_ty);

		dist = point_to_segment_dist(
			(float)tx, (float)ty,
			(float)from_tx, (float)from_ty,
			(float)to_tx, (float)to_ty);

		if (dist < best_dist) {
			best_dist = dist;
			best_idx = i;
		}
	}

	if (best_idx >= 0 && best_dist <= (float)threshold)
		return best_idx;
	return -1;
}
