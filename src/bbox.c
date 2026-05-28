#include "bbox.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

static const uint32_t default_colors[] = {
	0x00ff00, 0xff0000, 0x0000ff, 0xffff00,
	0xff00ff, 0x00ffff, 0xff8800, 0x8800ff
};

void bbox_manager_init(struct bbox_manager *bm)
{
	memset(bm, 0, sizeof(*bm));
	bm->selected = -1;
	bm->next_id = 1;
}

int bbox_add(struct bbox_manager *bm, int x, int y, int w, int h,
	     const char *label, uint32_t color)
{
	struct bbox *b;

	if (bm->count >= BBOX_MAX)
		return -1;

	b = &bm->boxes[bm->count];
	memset(b, 0, sizeof(*b));
	b->x = x;
	b->y = y;
	b->w = w;
	b->h = h;
	b->id = bm->next_id++;
	b->color = color ? color :
		   default_colors[bm->count % ARRAY_SIZE(default_colors)];
	if (label)
		strncpy(b->label, label, BBOX_LABEL_MAX - 1);

	bm->count++;
	bm->selected = bm->count - 1;
	return b->id;
}

int bbox_remove(struct bbox_manager *bm, int id)
{
	int i;

	for (i = 0; i < bm->count; i++) {
		if (bm->boxes[i].id == id) {
			memmove(&bm->boxes[i], &bm->boxes[i + 1],
				(bm->count - i - 1) * sizeof(struct bbox));
			bm->count--;
			if (bm->selected >= bm->count)
				bm->selected = bm->count - 1;
			return 0;
		}
	}
	return -1;
}

int bbox_find_at(struct bbox_manager *bm, int px, int py)
{
	int i;
	struct bbox *b;

	for (i = bm->count - 1; i >= 0; i--) {
		b = &bm->boxes[i];
		if (px >= b->x && px < b->x + b->w &&
		    py >= b->y && py < b->y + b->h)
			return i;
	}
	return -1;
}

int bbox_hit_handle(struct bbox *b, int px, int py, int handle_size)
{
	int hs;
	int cx, cy;

	hs = handle_size;

	cx = b->x;
	cy = b->y;
	if (px >= cx - hs && px <= cx + hs && py >= cy - hs && py <= cy + hs)
		return HANDLE_TL;

	cx = b->x + b->w;
	cy = b->y;
	if (px >= cx - hs && px <= cx + hs && py >= cy - hs && py <= cy + hs)
		return HANDLE_TR;

	cx = b->x;
	cy = b->y + b->h;
	if (px >= cx - hs && px <= cx + hs && py >= cy - hs && py <= cy + hs)
		return HANDLE_BL;

	cx = b->x + b->w;
	cy = b->y + b->h;
	if (px >= cx - hs && px <= cx + hs && py >= cy - hs && py <= cy + hs)
		return HANDLE_BR;

	if (px >= b->x && px < b->x + b->w &&
	    py >= b->y && py < b->y + b->h)
		return HANDLE_BODY;

	return HANDLE_NONE;
}

void bbox_mouse_down(struct bbox_manager *bm, int px, int py,
		     int button)
{
	int idx;
	struct bbox *b;
	int handle;

	if (button == BBOX_BTN_SECONDARY) {
		/*
		 * Right-button: select / move / resize an existing
		 * bbox. If the click misses every bbox, do nothing
		 * (no drag started).
		 */
		if (bm->selected >= 0 && bm->selected < bm->count) {
			b = &bm->boxes[bm->selected];
			handle = bbox_hit_handle(b, px, py, 8);
			if (handle == HANDLE_BODY) {
				bm->drag_state = DRAG_MOVE;
				bm->start_x = px;
				bm->start_y = py;
				bm->cur_x = px;
				bm->cur_y = py;
				bm->orig_x = b->x;
				bm->orig_y = b->y;
				bm->orig_w = b->w;
				bm->orig_h = b->h;
				return;
			}
			if (handle >= HANDLE_TL &&
			    handle <= HANDLE_BR) {
				bm->drag_state = DRAG_RESIZE;
				bm->drag_handle = handle;
				bm->start_x = px;
				bm->start_y = py;
				bm->cur_x = px;
				bm->cur_y = py;
				bm->orig_x = b->x;
				bm->orig_y = b->y;
				bm->orig_w = b->w;
				bm->orig_h = b->h;
				return;
			}
		}

		idx = bbox_find_at(bm, px, py);
		if (idx >= 0) {
			bm->selected = idx;
			b = &bm->boxes[idx];
			bm->drag_state = DRAG_MOVE;
			bm->start_x = px;
			bm->start_y = py;
			bm->cur_x = px;
			bm->cur_y = py;
			bm->orig_x = b->x;
			bm->orig_y = b->y;
			bm->orig_w = b->w;
			bm->orig_h = b->h;
			return;
		}
		return;
	}

	/*
	 * Left-button: always start a fresh "create" drag,
	 * regardless of whether the press lands inside an
	 * existing bbox. Use the right button to manipulate
	 * existing bboxes.
	 */
	bm->drag_state = DRAG_CREATE;
	bm->start_x = px;
	bm->start_y = py;
	bm->cur_x = px;
	bm->cur_y = py;
	bm->selected = -1;
}

void bbox_mouse_move(struct bbox_manager *bm, int px, int py)
{
	int dx, dy;
	struct bbox *b;

	dx = px - bm->start_x;
	dy = py - bm->start_y;
	bm->cur_x = px;
	bm->cur_y = py;

	if (bm->drag_state == DRAG_CREATE)
		return;

	if (bm->selected < 0 || bm->selected >= bm->count)
		return;

	b = &bm->boxes[bm->selected];

	if (bm->drag_state == DRAG_MOVE) {
		b->x = bm->orig_x + dx;
		b->y = bm->orig_y + dy;
	} else if (bm->drag_state == DRAG_RESIZE) {
		switch (bm->drag_handle) {
		case HANDLE_BR:
			b->w = bm->orig_w + dx;
			b->h = bm->orig_h + dy;
			break;
		case HANDLE_BL:
			b->x = bm->orig_x + dx;
			b->w = bm->orig_w - dx;
			b->h = bm->orig_h + dy;
			break;
		case HANDLE_TR:
			b->w = bm->orig_w + dx;
			b->y = bm->orig_y + dy;
			b->h = bm->orig_h - dy;
			break;
		case HANDLE_TL:
			b->x = bm->orig_x + dx;
			b->w = bm->orig_w - dx;
			b->y = bm->orig_y + dy;
			b->h = bm->orig_h - dy;
			break;
		}
	}
}

int bbox_mouse_up(struct bbox_manager *bm)
{
	int id;

	if (bm->drag_state == DRAG_CREATE) {
		int x, y, w, h;

		x = MIN(bm->start_x, bm->cur_x);
		y = MIN(bm->start_y, bm->cur_y);
		w = abs(bm->cur_x - bm->start_x);
		h = abs(bm->cur_y - bm->start_y);

		if (w < 2 && h < 2) {
			bm->drag_state = DRAG_NONE;
			return -1;
		}

		id = bbox_add(bm, x, y, w, h, NULL, 0);
		bm->drag_state = DRAG_NONE;
		return id;
	}

	bm->drag_state = DRAG_NONE;
	return -1;
}

void bbox_select_next(struct bbox_manager *bm)
{
	if (bm->count == 0)
		return;
	bm->selected = (bm->selected + 1) % bm->count;
}

void bbox_normalize(struct bbox *b)
{
	if (b->w < 0) {
		b->x += b->w;
		b->w = -b->w;
	}
	if (b->h < 0) {
		b->y += b->h;
		b->h = -b->h;
	}
}

struct bbox *bbox_get_selected(struct bbox_manager *bm)
{
	if (bm->selected < 0 || bm->selected >= bm->count)
		return NULL;
	return &bm->boxes[bm->selected];
}
