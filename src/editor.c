#include "editor.h"
#include "render.h"
#include "json_util.h"
#include "canvas.h"
#include "util.h"

#include "termbox2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int compute_img_rows(struct editor *ed)
{
	int img_rows = 0;
	int i;

	for (i = 0; i < ed->image_count; i++) {
		int r = ed->layout_slots[i].canvas_y +
			(ed->layout_slots[i].display_h + 1) / 2;
		if (r > img_rows)
			img_rows = r;
	}
	return MAX(img_rows, 0);
}

static void editor_draw_sidebar(struct editor *ed)
{
	int term_w, term_h;
	int sx, y, i, row;
	char buf[256];

	ed->sidebar_row_count = 0;
	for (i = 0; i < (int)(sizeof(ed->sidebar_row_to_id) /
			      sizeof(ed->sidebar_row_to_id[0])); i++)
		ed->sidebar_row_to_id[i] = -1;

	if (ed->render.sidebar_w <= 0)
		return;

	term_w = tb_width();
	term_h = tb_height();
	if (term_w <= 0 || term_h <= 0)
		return;

	sx = term_w - ed->render.sidebar_w;
	if (sx < 0)
		sx = 0;

	for (y = 0; y < term_h; y++)
		tb_set_cell(sx, y, '|', TB_WHITE, TB_DEFAULT);

	row = 0;
	tb_print(sx + 2, row++, TB_YELLOW | TB_BOLD, TB_DEFAULT, "BBoxes");
	for (i = 0; i < ed->render.sidebar_w - 3; i++)
		tb_set_cell(sx + 2 + i, row, '-', TB_WHITE, TB_DEFAULT);
	row++;

	if (ed->bboxes.count == 0) {
		tb_print(sx + 2, row, TB_WHITE, TB_DEFAULT, "(none)");
		row++;
	} else {
		for (i = 0; i < ed->bboxes.count && row < term_h - 1; i++) {
			struct bbox *b = &ed->bboxes.boxes[i];
			uintattr_t fg = TB_WHITE;
			uintattr_t bg = TB_DEFAULT;
			const char *label;
			int max_rows = (int)(sizeof(ed->sidebar_row_to_id) /
					     sizeof(ed->sidebar_row_to_id[0]));

			if (i == ed->bboxes.selected) {
				fg = TB_BLACK;
				bg = TB_YELLOW;
			}

			snprintf(buf, sizeof(buf), " #%d img%d %dx%d@%d,%d",
				 b->id, b->image_index, b->w, b->h,
				 b->x, b->y);
			if (row < max_rows)
				ed->sidebar_row_to_id[row] = b->id;
			tb_print(sx + 1, row++, fg, bg, buf);
			if (row >= term_h - 1)
				break;

			if (ed->mode == MODE_LABEL_EDIT &&
			    !ed->label_is_arrow &&
			    b->id == ed->label_target_id) {
				label = ed->label_buf[0]
					? ed->label_buf
					: "(typing...)";
			} else {
				label = b->label[0] ? b->label : "(no label)";
			}
			snprintf(buf, sizeof(buf), "  L: %s", label);
			if (row < max_rows)
				ed->sidebar_row_to_id[row] = b->id;
			tb_print(sx + 1, row++, fg, bg, buf);
			if (row >= term_h - 1)
				break;
		}
	}

	if (row < term_h - 1) {
		for (i = 0; i < ed->render.sidebar_w - 3; i++)
			tb_set_cell(sx + 2 + i, row, '-', TB_WHITE,
				    TB_DEFAULT);
		row++;
	}

	tb_print(sx + 2, row++, TB_CYAN | TB_BOLD, TB_DEFAULT, "Arrows");
	for (i = 0; i < ed->render.sidebar_w - 3; i++)
		tb_set_cell(sx + 2 + i, row, '-', TB_WHITE, TB_DEFAULT);
	row++;

	if (ed->arrows.count == 0) {
		tb_print(sx + 2, row, TB_WHITE, TB_DEFAULT, "(none)");
		row++;
	} else {
		for (i = 0; i < ed->arrows.count && row < term_h - 1; i++) {
			struct arrow *a = &ed->arrows.arrows[i];
			uintattr_t fg = TB_WHITE;
			uintattr_t bg = TB_DEFAULT;
			const char *label;
			int max_rows = (int)(sizeof(ed->sidebar_row_to_id) /
					     sizeof(ed->sidebar_row_to_id[0]));
			int sid;

			if (i == ed->arrows.selected) {
				fg = TB_BLACK;
				bg = TB_CYAN;
			}

			sid = -(a->id + 1);
			snprintf(buf, sizeof(buf),
				 " #A%d img%d->img%d",
				 a->id, a->from.image_index,
				 a->to.image_index);
			if (row < max_rows)
				ed->sidebar_row_to_id[row] = sid;
			tb_print(sx + 1, row++, fg, bg, buf);
			if (row >= term_h - 1)
				break;

			if (ed->mode == MODE_LABEL_EDIT &&
			    ed->label_is_arrow &&
			    a->id == ed->label_target_id) {
				label = ed->label_buf[0]
					? ed->label_buf
					: "(typing...)";
			} else {
				label = a->label[0] ? a->label : "(no label)";
			}
			snprintf(buf, sizeof(buf), "  L: %s", label);
			if (row < max_rows)
				ed->sidebar_row_to_id[row] = sid;
			tb_print(sx + 1, row++, fg, bg, buf);
			if (row >= term_h - 1)
				break;
		}
	}

	ed->sidebar_row_count = row;
}
static void editor_draw_bbox_overlay(struct editor *ed)
{
	int i;
	int sidebar_x;
	int term_w;

	term_w = tb_width();
	sidebar_x = (ed->render.sidebar_w > 0)
		? (term_w - ed->render.sidebar_w)
		: term_w;

	for (i = 0; i < ed->bboxes.count; i++) {
		struct bbox *b = &ed->bboxes.boxes[i];
		int is_sel = (i == ed->bboxes.selected);
		uintattr_t fg = is_sel
			? (TB_YELLOW | TB_BOLD)
			: TB_GREEN;
		int bx, by, bw, bh;
		int x0, y0, x1, y1;
		int cx, cy;
		int img_cols, img_rows;
		struct layout_slot *slot;

		if (b->image_index < 0 || b->image_index >= ed->image_count)
			continue;

		slot = &ed->layout_slots[b->image_index];
		if (slot->scale <= 0.0f)
			continue;

		bx = b->x;
		by = b->y;
		bw = b->w;
		bh = b->h;
		if (bw < 0) { bx += bw; bw = -bw; }
		if (bh < 0) { by += bh; bh = -bh; }

		img_cols = slot->display_w;
		img_rows = (slot->display_h + 1) / 2;

		x0 = slot->canvas_x + (int)(bx * slot->scale);
		y0 = slot->canvas_y + (int)(by * slot->scale) / 2;
		x1 = slot->canvas_x + (int)((bx + bw) * slot->scale) - 1;
		y1 = slot->canvas_y + (int)((by + bh) * slot->scale) / 2 - 1;

		if (x0 < slot->canvas_x) x0 = slot->canvas_x;
		if (y0 < slot->canvas_y) y0 = slot->canvas_y;
		if (x1 >= slot->canvas_x + img_cols)
			x1 = slot->canvas_x + img_cols - 1;
		if (y1 >= slot->canvas_y + img_rows)
			y1 = slot->canvas_y + img_rows - 1;
		if (x1 >= sidebar_x) x1 = sidebar_x - 1;
		if (x1 < x0 || y1 < y0) continue;

		for (cx = x0; cx <= x1; cx++) {
			tb_set_cell(cx, y0, 0x2500, fg, TB_DEFAULT);
			tb_set_cell(cx, y1, 0x2500, fg, TB_DEFAULT);
		}
		for (cy = y0; cy <= y1; cy++) {
			tb_set_cell(x0, cy, 0x2502, fg, TB_DEFAULT);
			tb_set_cell(x1, cy, 0x2502, fg, TB_DEFAULT);
		}
		tb_set_cell(x0, y0, 0x250C, fg, TB_DEFAULT);
		tb_set_cell(x1, y0, 0x2510, fg, TB_DEFAULT);
		tb_set_cell(x0, y1, 0x2514, fg, TB_DEFAULT);
		tb_set_cell(x1, y1, 0x2518, fg, TB_DEFAULT);

		if (is_sel) {
			tb_set_cell(x0, y0, '+', TB_BLACK, TB_YELLOW);
			tb_set_cell(x1, y0, '+', TB_BLACK, TB_YELLOW);
			tb_set_cell(x0, y1, '+', TB_BLACK, TB_YELLOW);
			tb_set_cell(x1, y1, '+', TB_BLACK, TB_YELLOW);
		}

		if (b->label[0]) {
			int lx = x0 + 1;
			int ly = (y0 > slot->canvas_y) ? y0 - 1 : y0;

			if (lx < sidebar_x)
				tb_print(lx, ly, fg, TB_DEFAULT, b->label);
		}
	}

	if (ed->bboxes.drag_state == DRAG_CREATE) {
		struct layout_slot *slot;
		int dsx, dsy, ex, ey;
		int x0, y0, x1, y1, cx, cy;
		int img_cols, img_rows;

		if (ed->drag_image_index < 0 ||
		    ed->drag_image_index >= ed->image_count)
			return;
		slot = &ed->layout_slots[ed->drag_image_index];
		if (slot->scale <= 0.0f)
			return;

		img_cols = slot->display_w;
		img_rows = (slot->display_h + 1) / 2;

		dsx = ed->bboxes.start_x;
		dsy = ed->bboxes.start_y;
		ex = ed->bboxes.cur_x;
		ey = ed->bboxes.cur_y;

		if (dsx > ex) { int t = dsx; dsx = ex; ex = t; }
		if (dsy > ey) { int t = dsy; dsy = ey; ey = t; }
		x0 = slot->canvas_x + (int)(dsx * slot->scale);
		y0 = slot->canvas_y + (int)(dsy * slot->scale) / 2;
		x1 = slot->canvas_x + (int)(ex * slot->scale) - 1;
		y1 = slot->canvas_y + (int)(ey * slot->scale) / 2 - 1;
		if (x0 < slot->canvas_x) x0 = slot->canvas_x;
		if (y0 < slot->canvas_y) y0 = slot->canvas_y;
		if (x1 >= slot->canvas_x + img_cols)
			x1 = slot->canvas_x + img_cols - 1;
		if (y1 >= slot->canvas_y + img_rows)
			y1 = slot->canvas_y + img_rows - 1;
		if (x1 >= sidebar_x) x1 = sidebar_x - 1;
		if (x1 >= x0 && y1 >= y0) {
			for (cx = x0; cx <= x1; cx++) {
				tb_set_cell(cx, y0, 0x2550,
					    TB_CYAN | TB_BOLD, TB_DEFAULT);
				tb_set_cell(cx, y1, 0x2550,
					    TB_CYAN | TB_BOLD, TB_DEFAULT);
			}
			for (cy = y0; cy <= y1; cy++) {
				tb_set_cell(x0, cy, 0x2551,
					    TB_CYAN | TB_BOLD, TB_DEFAULT);
				tb_set_cell(x1, cy, 0x2551,
					    TB_CYAN | TB_BOLD, TB_DEFAULT);
			}
		}
	}
}
static void editor_draw_arrow_overlay(struct editor *ed)
{
	int i;
	int sidebar_x;
	int term_w;

	term_w = tb_width();
	sidebar_x = (ed->render.sidebar_w > 0)
		? (term_w - ed->render.sidebar_w)
		: term_w;

	for (i = 0; i < ed->arrows.count; i++) {
		struct arrow *a = &ed->arrows.arrows[i];
		int is_sel = (i == ed->arrows.selected);
		uintattr_t fg = is_sel
			? (TB_YELLOW | TB_BOLD)
			: TB_RED;
		int from_tx, from_ty, to_tx, to_ty;

		if (a->from.image_index < 0 ||
		    a->from.image_index >= ed->image_count)
			continue;
		if (a->to.image_index < 0 ||
		    a->to.image_index >= ed->image_count)
			continue;

		pixel_to_term_multi(a->from.x, a->from.y,
				    a->from.image_index,
				    &from_tx, &from_ty,
				    ed->layout_slots, ed->image_count);
		pixel_to_term_multi(a->to.x, a->to.y,
				    a->to.image_index,
				    &to_tx, &to_ty,
				    ed->layout_slots, ed->image_count);

		if (from_tx >= sidebar_x || to_tx >= sidebar_x)
			continue;

		{
			int adx = abs(to_tx - from_tx);
			int ady = abs(to_ty - from_ty);
			uint32_t head_ch;
			int n_steps;
			int prev_cx = -999, prev_cy = -999;
			int step;

			if (adx >= ady * 2)
				head_ch = (to_tx > from_tx) ? '>' : '<';
			else if (ady >= adx * 2)
				head_ch = (to_ty > from_ty) ? 'v' : '^';
			else if (to_tx > from_tx)
				head_ch = (to_ty > from_ty) ?
					0x2198 : 0x2197;
			else
				head_ch = (to_ty > from_ty) ?
					0x2199 : 0x2196;

			n_steps = MAX(adx, ady);
			if (n_steps == 0)
				n_steps = 1;

			for (step = 0; step <= n_steps; step++) {
				int nx = from_tx +
					(int)((long long)(to_tx - from_tx)
					      * step / n_steps);
				int ny = from_ty +
					(int)((long long)(to_ty - from_ty)
					      * step / n_steps);

				if (nx == prev_cx && ny == prev_cy)
					continue;
				prev_cx = nx;
				prev_cy = ny;

				if (nx == to_tx && ny == to_ty)
					continue;
				if (nx >= 0 && nx < sidebar_x &&
				    ny >= 0)
					tb_set_cell(nx, ny, '.',
						    fg, TB_DEFAULT);
			}

			tb_set_cell(to_tx, to_ty, head_ch,
				    fg | TB_BOLD, TB_DEFAULT);
		}

		if (a->label[0]) {
			int lx = (from_tx + to_tx) / 2;
			int ly = (from_ty + to_ty) / 2 - 1;

			if (lx >= 0 && lx < sidebar_x && ly >= 0)
				tb_print(lx, ly, fg, TB_DEFAULT, a->label);
		}
	}

	if (ed->arrows.drag_state == ARROW_DRAG_FROM_SET) {
		int ftx, fty;

		if (ed->arrows.drag_from.image_index >= 0 &&
		    ed->arrows.drag_from.image_index < ed->image_count) {
			pixel_to_term_multi(
				ed->arrows.drag_from.x,
				ed->arrows.drag_from.y,
				ed->arrows.drag_from.image_index,
				&ftx, &fty,
				ed->layout_slots, ed->image_count);
			if (ftx >= 0 && ftx < sidebar_x && fty >= 0)
				tb_set_cell(ftx, fty, 'o',
					    TB_CYAN | TB_BOLD, TB_DEFAULT);
		}
	}
}
static const char *mode_str(enum editor_mode mode)
{
	switch (mode) {
	case MODE_VIEW: return "VIEW";
	case MODE_SELECT: return "SELECT";
	case MODE_CONFIRM: return "CONFIRM";
	case MODE_LABEL_EDIT: return "LABEL";
	case MODE_ARROW_DRAW: return "ARROW";
	default: return "VIEW";
	}
}

static void editor_redraw_status_bar_fast(struct editor *ed)
{
	int term_w, term_h;
	int x;
	char line1[256], line2[256];
	int img_rows;

	term_w = tb_width();
	term_h = tb_height();
	if (term_w <= 0 || term_h <= 0)
		return;

	tb_clear();
	img_rows = compute_img_rows(ed);

	for (x = 0; x < term_w; x++)
		tb_set_cell(x, img_rows, '-', TB_WHITE, TB_DEFAULT);

	snprintf(line1, sizeof(line1),
		 " imgs:%d | BBox:%d Arrow:%d | %s | Undo: %d",
		 ed->image_count,
		 ed->bboxes.count, ed->arrows.count,
		 mode_str(ed->mode), ed->undo_count);
	tb_print(0, img_rows + 1, TB_WHITE, TB_DEFAULT, line1);

	if (ed->mode == MODE_LABEL_EDIT) {
		snprintf(line2, sizeof(line2),
			 " Label: %s_  (Enter=ok, ESC=cancel)",
			 ed->label_buf);
	} else {
		snprintf(line2, sizeof(line2),
			 " L=draw R=select [a]dd [d]el [e]label [x]arrow [s]ave [u]ndo [r]edo [q]uit");
	}
	tb_print(0, img_rows + 2, TB_WHITE, TB_DEFAULT, line2);

	editor_draw_sidebar(ed);
	editor_draw_bbox_overlay(ed);
	editor_draw_arrow_overlay(ed);
	tb_present();
	ed->render.dirty = 0;
}

static void editor_do_render(struct editor *ed)
{
	int term_w, term_h;
	int x, i;
	char line1[256], line2[256];
	int img_rows;

	term_w = tb_width();
	term_h = tb_height();

	if (term_w <= 0 || term_h <= 0)
		return;

	if (ed->image_count <= 0)
		return;

	{
		int img_w_arr[MAX_IMAGES];
		int img_h_arr[MAX_IMAGES];

		for (i = 0; i < ed->image_count; i++) {
			img_w_arr[i] = ed->images[i].img.width;
			img_h_arr[i] = ed->images[i].img.height;
		}
		render_calc_fit_multi(ed->image_count, img_w_arr, img_h_arr,
				      term_w, term_h, ed->layout_slots);
		if (term_w >= 60)
			ed->render.sidebar_w = SIDEBAR_W;
		else
			ed->render.sidebar_w = 0;
		for (i = 0; i < ed->image_count; i++) {
			ed->images[i].canvas_x =
				ed->layout_slots[i].canvas_x;
			ed->images[i].canvas_y =
				ed->layout_slots[i].canvas_y;
			ed->images[i].display_w =
				ed->layout_slots[i].display_w;
			ed->images[i].display_h =
				ed->layout_slots[i].display_h;
			ed->images[i].scale =
				ed->layout_slots[i].scale;
		}
	}

	img_rows = compute_img_rows(ed);

	tb_clear();

	for (x = 0; x < term_w; x++)
		tb_set_cell(x, img_rows, '-', TB_WHITE, TB_DEFAULT);

	snprintf(line1, sizeof(line1),
		 " imgs:%d | BBox:%d Arrow:%d | %s | Undo: %d",
		 ed->image_count,
		 ed->bboxes.count, ed->arrows.count,
		 mode_str(ed->mode), ed->undo_count);
	tb_print(0, img_rows + 1, TB_WHITE, TB_DEFAULT, line1);

	if (ed->mode == MODE_LABEL_EDIT) {
		snprintf(line2, sizeof(line2),
			 " Label: %s_  (Enter=ok, ESC=cancel)",
			 ed->label_buf);
	} else {
		snprintf(line2, sizeof(line2),
			 " L=draw R=select [a]dd [d]el [e]label [x]arrow [s]ave [u]ndo [r]edo [q]uit");
	}
	tb_print(0, img_rows + 2, TB_WHITE, TB_DEFAULT, line2);

	editor_draw_sidebar(ed);

	{
		int need_image_upload =
			(!ed->image_uploaded) ||
			(ed->image_gen != ed->last_uploaded_gen) ||
			(term_w != ed->last_term_w) ||
			(term_h != ed->last_term_h);

		if (need_image_upload) {
			tb_send("\033[?2026h", 8);
			tb_present();
			tb_send("\033[H", 3);

			if (ed->image_count == 1) {
				struct image_slot *sl = &ed->images[0];
				char *img_data = NULL;

				if (ed->proto == TERM_PROTO_KITTY) {
					int dc = sl->display_w;
					int dr = (sl->display_h + 1) / 2;
					if (dc < 1) dc = 1;
					if (dr < 1) dr = 1;
					img_data = render_image_kitty(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						sl->img.pixels,
						sl->img.width,
						sl->img.height,
						sl->img.channels,
						ed->render
							.kitty_placement_id,
						dc, dr);
				} else if (ed->proto ==
					   TERM_PROTO_ITERM2) {
					int dc = sl->display_w;
					int dr = (sl->display_h + 1) / 2;
					if (dc < 1) dc = 1;
					if (dr < 1) dr = 1;
					img_data = render_image_iterm2(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						sl->img.pixels,
						sl->img.width,
						sl->img.height,
						sl->img.channels,
						dc, dr);
				} else {
					unsigned char *dp;
					dp = image_resize(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						sl->img.pixels,
						sl->img.width,
						sl->img.height,
						sl->img.channels,
						sl->display_w,
						sl->display_h);
					if (dp) {
						img_data =
							render_image_sixel(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						dp, sl->display_w,
						sl->display_h,
						sl->img.channels);
					}
				}
				if (img_data)
					tb_send(img_data,
						strlen(img_data));
			} else {
				int cw = 0, ch = 0;
				struct canvas_layer layers[MAX_IMAGES];
				unsigned char *composite_pixels;
				char *img_data = NULL;

				for (i = 0; i < ed->image_count; i++) {
					int r;
					r = ed->layout_slots[i].canvas_y +
						(ed->layout_slots[i].display_h + 1) / 2;
					if (r > ch)
						ch = r;
					r = ed->layout_slots[i].canvas_x +
						ed->layout_slots[i].display_w;
					if (r > cw)
						cw = r;
				}
				ch *= 2;

				for (i = 0; i < ed->image_count; i++) {
					struct image_slot *sl = &ed->images[i];
					unsigned char *resized;

					memset(&layers[i], 0,
					       sizeof(layers[i]));
					resized = image_resize(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						sl->img.pixels,
						sl->img.width,
						sl->img.height,
						sl->img.channels,
						sl->display_w,
						sl->display_h);
					if (resized) {
						layers[i].pixels = resized;
						layers[i].w = sl->display_w;
						layers[i].h = sl->display_h;
						layers[i].x =
						  ed->layout_slots[i]
						  	.canvas_x;
						layers[i].y =
						  ed->layout_slots[i]
						  	.canvas_y * 2;
						layers[i].opacity = 1.0f;
						layers[i].blend =
							BLEND_NORMAL;
					}
				}

				composite_pixels = canvas_composite(
					&ed->arenas.arenas[ARENA_COMMAND],
					cw, ch, layers,
					ed->image_count, 0x1a1a2e);

				if (composite_pixels) {
					if (ed->proto ==
					    TERM_PROTO_KITTY) {
						img_data =
						  render_image_kitty(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						composite_pixels,
						cw, ch, 4,
						ed->render
							.kitty_placement_id,
						cw, ch / 2);
					} else if (ed->proto ==
						   TERM_PROTO_ITERM2) {
						img_data =
						  render_image_iterm2(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						composite_pixels,
						cw, ch, 4,
						cw, ch / 2);
					} else {
						img_data =
						  render_image_sixel(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						composite_pixels,
						cw, ch, 4);
					}
					if (img_data)
						tb_send(img_data,
							strlen(img_data));
				}
			}

			tb_present();
			ed->image_uploaded = 1;
			ed->last_uploaded_gen = ed->image_gen;
			ed->last_term_w = term_w;
			ed->last_term_h = term_h;
			tb_send("\033[?2026l", 8);
		}

		editor_draw_bbox_overlay(ed);
		editor_draw_arrow_overlay(ed);
		tb_present();
	}

	(void)x;
	ed->render.dirty = 0;
	arena_reset(&ed->arenas.arenas[ARENA_COMMAND]);
}
int editor_init(struct editor *ed)
{
	memset(ed, 0, sizeof(*ed));
	arena_set_init(&ed->arenas);
	bbox_manager_init(&ed->bboxes);
	arrow_manager_init(&ed->arrows);
	history_init(&ed->history, 64);
	render_init(&ed->render, TERM_PROTO_UNKNOWN);
	ed->mode = MODE_VIEW;
	ed->running = 0;
	ed->undo_count = 0;
	ed->label_target_id = -1;
	ed->drag_image_index = -1;
	return 0;
}

void editor_free(struct editor *ed)
{
	int i;

	if (ed->running)
		editor_stop(ed);
	for (i = 0; i < ed->image_count; i++)
		image_free(&ed->images[i].img);
	history_free(&ed->history);
	arena_set_free(&ed->arenas);
}

int editor_open_images(struct editor *ed, const char **paths, int count)
{
	int i;

	if (count <= 0 || count > MAX_IMAGES)
		return -1;

	arena_reset(&ed->arenas.arenas[ARENA_IMAGE]);
	ed->image_count = 0;

	for (i = 0; i < count; i++) {
		int ret;

		ret = image_load(&ed->arenas.arenas[ARENA_IMAGE],
				 paths[i], &ed->images[i].img);
		if (ret < 0) {
			int j;

			for (j = 0; j < i; j++)
				image_free(&ed->images[j].img);
			ed->image_count = 0;
			return ret;
		}
	}

	ed->image_count = count;
	ed->image_gen++;
	ed->image_uploaded = 0;
	ed->render.dirty = 1;
	return 0;
}

int editor_open_image_base64(struct editor *ed, const char *b64)
{
	int ret;

	arena_reset(&ed->arenas.arenas[ARENA_IMAGE]);
	ret = image_load_base64(&ed->arenas.arenas[ARENA_IMAGE], b64,
				&ed->images[0].img);
	if (ret < 0)
		return ret;
	ed->image_count = 1;
	ed->image_gen++;
	ed->image_uploaded = 0;
	ed->render.dirty = 1;
	return 0;
}

static cJSON *snapshot_state(struct editor *ed)
{
	cJSON *snap;
	cJSON *arr;
	int i;

	snap = cJSON_CreateObject();
	if (!snap)
		return NULL;

	arr = cJSON_CreateArray();
	for (i = 0; i < ed->bboxes.count; i++)
		cJSON_AddItemToArray(arr,
				     bbox_to_json(&ed->bboxes.boxes[i]));
	cJSON_AddItemToObject(snap, "boxes", arr);
	cJSON_AddNumberToObject(snap, "selected", ed->bboxes.selected);
	cJSON_AddNumberToObject(snap, "next_id", ed->bboxes.next_id);

	arr = cJSON_CreateArray();
	for (i = 0; i < ed->arrows.count; i++)
		cJSON_AddItemToArray(arr,
				     arrow_to_json(&ed->arrows.arrows[i]));
	cJSON_AddItemToObject(snap, "arrows", arr);
	cJSON_AddNumberToObject(snap, "arrow_selected",
				ed->arrows.selected);
	cJSON_AddNumberToObject(snap, "arrow_next_id",
				ed->arrows.next_id);

	return snap;
}

static void restore_state(struct editor *ed, cJSON *snap)
{
	cJSON *arr;
	cJSON *item;
	cJSON *tmp;
	int i;

	if (!snap)
		return;

	arr = cJSON_GetObjectItem(snap, "boxes");
	ed->bboxes.count = 0;
	if (arr) {
		i = 0;
		cJSON_ArrayForEach(item, arr) {
			if (i >= BBOX_MAX)
				break;
			ed->bboxes.boxes[i] = bbox_from_json(item);
			i++;
		}
		ed->bboxes.count = i;
	}
	tmp = cJSON_GetObjectItem(snap, "selected");
	ed->bboxes.selected = tmp ? tmp->valueint : -1;
	tmp = cJSON_GetObjectItem(snap, "next_id");
	if (tmp)
		ed->bboxes.next_id = tmp->valueint;
	ed->bboxes.drag_state = DRAG_NONE;

	arr = cJSON_GetObjectItem(snap, "arrows");
	ed->arrows.count = 0;
	if (arr) {
		i = 0;
		cJSON_ArrayForEach(item, arr) {
			if (i >= ARROW_MAX)
				break;
			ed->arrows.arrows[i] = arrow_from_json(item);
			i++;
		}
		ed->arrows.count = i;
	}
	tmp = cJSON_GetObjectItem(snap, "arrow_selected");
	ed->arrows.selected = tmp ? tmp->valueint : -1;
	tmp = cJSON_GetObjectItem(snap, "arrow_next_id");
	if (tmp)
		ed->arrows.next_id = tmp->valueint;
	ed->arrows.drag_state = ARROW_DRAG_NONE;
}

static void editor_push_history(struct editor *ed, enum op_type type)
{
	cJSON *snap;

	snap = snapshot_state(ed);
	if (snap)
		history_push(&ed->history, type, snap);
}

static char *editor_dump_json(struct editor *ed)
{
	cJSON *root;
	cJSON *arr;
	char *result;
	int i;

	root = cJSON_CreateObject();

	if (ed->image_count == 1) {
		cJSON_AddStringToObject(root, "path",
					ed->images[0].img.path);
		cJSON_AddNumberToObject(root, "width",
					ed->images[0].img.width);
		cJSON_AddNumberToObject(root, "height",
					ed->images[0].img.height);
	} else {
		cJSON *images_arr = cJSON_CreateArray();
		for (i = 0; i < ed->image_count; i++) {
			cJSON *img_obj = cJSON_CreateObject();
			cJSON_AddStringToObject(img_obj, "path",
						ed->images[i].img.path);
			cJSON_AddNumberToObject(img_obj, "width",
						ed->images[i].img.width);
			cJSON_AddNumberToObject(img_obj, "height",
						ed->images[i].img.height);
			cJSON_AddItemToArray(images_arr, img_obj);
		}
		cJSON_AddItemToObject(root, "images", images_arr);
	}

	arr = cJSON_CreateArray();
	for (i = 0; i < ed->bboxes.count; i++)
		cJSON_AddItemToArray(arr,
				     bbox_to_json(&ed->bboxes.boxes[i]));
	cJSON_AddItemToObject(root, "bboxes", arr);

	arr = cJSON_CreateArray();
	for (i = 0; i < ed->arrows.count; i++)
		cJSON_AddItemToArray(arr,
				     arrow_to_json(&ed->arrows.arrows[i]));
	cJSON_AddItemToObject(root, "arrows", arr);

	result = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return result;
}

static void editor_save(struct editor *ed)
{
	char *result;

	result = editor_dump_json(ed);
	if (result) {
		fprintf(stderr, "%s\n", result);
		free(result);
	}
}

static void editor_dump_on_quit(struct editor *ed)
{
	char *result;

	result = editor_dump_json(ed);
	if (result) {
		fprintf(stdout, "%s\n", result);
		fflush(stdout);
		free(result);
	}
}
static void editor_enter_label_edit(struct editor *ed, int target_id,
				    int is_arrow, const char *initial)
{
	ed->mode = MODE_LABEL_EDIT;
	ed->label_target_id = target_id;
	ed->label_is_arrow = is_arrow;
	if (initial) {
		strncpy(ed->label_buf, initial, ARROW_LABEL_MAX - 1);
		ed->label_buf[ARROW_LABEL_MAX - 1] = '\0';
		ed->label_len = (int)strlen(ed->label_buf);
	} else {
		ed->label_buf[0] = '\0';
		ed->label_len = 0;
	}
	ed->render.dirty = 1;
}

static void editor_commit_label(struct editor *ed)
{
	if (ed->label_is_arrow) {
		struct arrow *a = NULL;
		int i;

		for (i = 0; i < ed->arrows.count; i++) {
			if (ed->arrows.arrows[i].id ==
			    ed->label_target_id) {
				a = &ed->arrows.arrows[i];
				break;
			}
		}
		if (a) {
			strncpy(a->label, ed->label_buf,
				ARROW_LABEL_MAX - 1);
			a->label[ARROW_LABEL_MAX - 1] = '\0';
		}
	} else {
		struct bbox *target = NULL;
		int i;

		for (i = 0; i < ed->bboxes.count; i++) {
			if (ed->bboxes.boxes[i].id ==
			    ed->label_target_id) {
				target = &ed->bboxes.boxes[i];
				break;
			}
		}
		if (target) {
			strncpy(target->label, ed->label_buf,
				BBOX_LABEL_MAX - 1);
			target->label[BBOX_LABEL_MAX - 1] = '\0';
		}
	}
	ed->mode = MODE_VIEW;
	ed->label_buf[0] = '\0';
	ed->label_len = 0;
	ed->label_target_id = -1;
	ed->render.dirty = 1;
}

static void editor_cancel_label(struct editor *ed)
{
	ed->mode = MODE_VIEW;
	ed->label_buf[0] = '\0';
	ed->label_len = 0;
	ed->label_target_id = -1;
	ed->render.dirty = 1;
}

static void label_handle_char(struct editor *ed, uint32_t ch)
{
	unsigned int cp = ch;
	char enc[4];
	int n = 0;
	int k;

	if (cp < 0x20)
		return;
	if (cp < 0x80) {
		enc[n++] = (char)cp;
	} else if (cp < 0x800) {
		enc[n++] = (char)(0xC0 | (cp >> 6));
		enc[n++] = (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		enc[n++] = (char)(0xE0 | (cp >> 12));
		enc[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		enc[n++] = (char)(0x80 | (cp & 0x3F));
	} else {
		enc[n++] = (char)(0xF0 | (cp >> 18));
		enc[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
		enc[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		enc[n++] = (char)(0x80 | (cp & 0x3F));
	}
	if (ed->label_len + n < ARROW_LABEL_MAX - 1) {
		for (k = 0; k < n; k++)
			ed->label_buf[ed->label_len++] = enc[k];
		ed->label_buf[ed->label_len] = '\0';
		ed->render.dirty = 1;
	}
}

static void label_handle_backspace(struct editor *ed)
{
	if (ed->label_len > 0) {
		int last_start = 0;
		int j;

		for (j = 0; j < ed->label_len; j++) {
			unsigned char c =
				(unsigned char)ed->label_buf[j];
			if ((c & 0xC0) != 0x80)
				last_start = j;
		}
		ed->label_len = last_start;
		ed->label_buf[ed->label_len] = '\0';
		ed->render.dirty = 1;
	}
}

void editor_handle_event(struct editor *ed, struct tb_event *ev)
{
	if (ev->type == TB_EVENT_RESIZE) {
		ed->render.dirty = 1;
		return;
	}

	if (ev->type == TB_EVENT_MOUSE) {
		int px, py, img_idx;
		int sidebar_x;
		int term_w;

		term_w = tb_width();
		sidebar_x = (ed->render.sidebar_w > 0)
			? (term_w - ed->render.sidebar_w)
			: -1;

		if (sidebar_x >= 0 && ev->x > sidebar_x) {
			if (ev->key == TB_KEY_MOUSE_LEFT &&
			    ed->bboxes.drag_state == DRAG_NONE &&
			    ev->y >= 0 &&
			    ev->y < (int)(sizeof(ed->sidebar_row_to_id) /
					  sizeof(ed->sidebar_row_to_id[0]))) {
				int hit_id;

				hit_id =
					ed->sidebar_row_to_id[ev->y];
				if (hit_id >= 0) {
					int k;

					for (k = 0;
					     k < ed->bboxes.count;
					     k++) {
						if (ed->bboxes.boxes[k].id
						    == hit_id) {
							ed->bboxes
								.selected = k;
							ed->mode =
								MODE_VIEW;
							ed->render
								.dirty = 1;
							break;
						}
					}
				} else if (hit_id < -1) {
					int aid = -(hit_id + 1);
					int k;

					for (k = 0;
					     k < ed->arrows.count;
					     k++) {
						if (ed->arrows.arrows[k].id
						    == aid) {
							ed->arrows
								.selected = k;
							ed->mode =
								MODE_VIEW;
							ed->render
								.dirty = 1;
							break;
						}
					}
				}
			}
			return;
		}

		if (ed->mode == MODE_ARROW_DRAW) {
			if (ev->key == TB_KEY_MOUSE_LEFT) {
				if (term_to_pixel_multi(
					    ev->x, ev->y,
					    &px, &py, &img_idx,
					    ed->layout_slots,
					    ed->image_count) < 0)
					return;

				if (ed->arrows.drag_state ==
				    ARROW_DRAG_NONE) {
					ed->arrows.drag_from.image_index =
						img_idx;
					ed->arrows.drag_from.x = px;
					ed->arrows.drag_from.y = py;
					ed->arrows.drag_state =
						ARROW_DRAG_FROM_SET;
					ed->render.dirty = 1;
				} else if (ed->arrows.drag_state ==
					   ARROW_DRAG_FROM_SET) {
					struct arrow_point from, to;

					from = ed->arrows.drag_from;
					to.image_index = img_idx;
					to.x = px;
					to.y = py;

					editor_push_history(ed,
							    OP_ADD_ARROW);
					arrow_add(&ed->arrows, from, to,
						  NULL, 0);
					ed->arrows.drag_state =
						ARROW_DRAG_NONE;
					ed->mode = MODE_VIEW;

					editor_enter_label_edit(
						ed,
						ed->arrows.arrows[
							ed->arrows.count-1].id,
						1, NULL);
				}
			} else if (ev->key == TB_KEY_MOUSE_RIGHT) {
				ed->arrows.drag_state = ARROW_DRAG_NONE;
				ed->mode = MODE_VIEW;
				ed->render.dirty = 1;
			}
			return;
		}

		{
			int in_image = (term_to_pixel_multi(
					ev->x, ev->y,
					&px, &py, &img_idx,
					ed->layout_slots,
					ed->image_count) == 0);

			if (!in_image) {
				if (ev->key == TB_KEY_MOUSE_RELEASE &&
				    ed->bboxes.drag_state != DRAG_NONE) {
					int id;

					id = bbox_mouse_up(&ed->bboxes);
					if (id >= 0)
						ed->mode = MODE_CONFIRM;
					else
						ed->mode = MODE_VIEW;
					ed->render.dirty = 1;
				}
				return;
			}

			ed->drag_image_index = img_idx;

			if (ev->key == TB_KEY_MOUSE_LEFT ||
			    ev->key == TB_KEY_MOUSE_RIGHT) {
				if (ed->bboxes.drag_state == DRAG_NONE) {
					int btn;

					btn = (ev->key ==
					       TB_KEY_MOUSE_RIGHT)
						? BBOX_BTN_SECONDARY
						: BBOX_BTN_PRIMARY;
					editor_push_history(ed,
							    OP_ADD_BBOX);
					bbox_mouse_down(&ed->bboxes,
							px, py, btn);
				} else {
					bbox_mouse_move(&ed->bboxes,
							px, py);
				}
				ed->mode = MODE_SELECT;
				ed->render.dirty = 1;
			} else if (ev->key == TB_KEY_MOUSE_RELEASE) {
				if (ed->bboxes.drag_state != DRAG_NONE) {
					int id;

					id = bbox_mouse_up(&ed->bboxes);
					if (id >= 0) {
						editor_push_history(
							ed,
							OP_EDIT_LABEL);
						if (ed->bboxes.count > 0) {
							struct bbox *nb;
							nb = &ed->bboxes.boxes[
								ed->bboxes.count-1];
							nb->image_index =
								img_idx;
						}
						editor_enter_label_edit(
							ed, id, 0, NULL);
					} else {
						ed->mode = MODE_VIEW;
					}
					ed->render.dirty = 1;
				}
			}
		}
		return;
	}

	if (ev->type != TB_EVENT_KEY)
		return;

	if (ed->mode == MODE_LABEL_EDIT) {
		if (ev->key == TB_KEY_ENTER) {
			editor_commit_label(ed);
			return;
		}
		if (ev->key == TB_KEY_ESC) {
			editor_cancel_label(ed);
			return;
		}
		if (ev->key == TB_KEY_BACKSPACE ||
		    ev->key == TB_KEY_BACKSPACE2) {
			label_handle_backspace(ed);
			return;
		}
		if (ev->ch != 0)
			label_handle_char(ed, ev->ch);
		return;
	}

	switch (ev->key) {
	case 'q':
		ed->running = 0;
		return;
	case 'a': {
		int bw, bh, bx, by;

		if (ed->image_count <= 0)
			return;
		editor_push_history(ed, OP_ADD_BBOX);
		bw = ed->images[0].img.width / 4;
		bh = ed->images[0].img.height / 4;
		bx = ed->images[0].img.width / 2 - bw / 2;
		by = ed->images[0].img.height / 2 - bh / 2;
		bbox_add(&ed->bboxes, bx, by, bw, bh, 0, NULL, 0);
		ed->mode = MODE_SELECT;
		ed->render.dirty = 1;
		return;
	}
	case 's':
		editor_save(ed);
		return;
	case 'e': {
		if (ed->label_is_arrow ||
		    ed->arrows.selected >= 0) {
			struct arrow *sel =
				arrow_get_selected(&ed->arrows);
			if (sel) {
				editor_push_history(ed,
						    OP_EDIT_ARROW_LABEL);
				editor_enter_label_edit(
					ed, sel->id, 1, sel->label);
				return;
			}
		}
		{
			struct bbox *sel =
				bbox_get_selected(&ed->bboxes);
			if (sel) {
				editor_push_history(ed,
						    OP_EDIT_LABEL);
				editor_enter_label_edit(
					ed, sel->id, 0, sel->label);
			}
		}
		return;
	}
	case 'x':
		ed->mode = MODE_ARROW_DRAW;
		ed->arrows.drag_state = ARROW_DRAG_NONE;
		ed->render.dirty = 1;
		return;
	case 'd': {
		struct arrow *as = arrow_get_selected(&ed->arrows);

		if (as) {
			editor_push_history(ed, OP_REMOVE_ARROW);
			arrow_remove(&ed->arrows, as->id);
			ed->render.dirty = 1;
			return;
		}
		{
			struct bbox *sel =
				bbox_get_selected(&ed->bboxes);
			if (sel) {
				editor_push_history(ed,
						    OP_REMOVE_BBOX);
				bbox_remove(&ed->bboxes, sel->id);
				ed->render.dirty = 1;
			}
		}
		return;
	}
	case 'u':
		if (history_can_undo(&ed->history)) {
			cJSON *cur = snapshot_state(ed);
			cJSON *prev = history_undo(&ed->history, cur);

			if (prev) {
				restore_state(ed, prev);
				cJSON_Delete(prev);
				ed->undo_count++;
				ed->render.dirty = 1;
			} else {
				cJSON_Delete(cur);
			}
		}
		return;
	case 'r':
		if (history_can_redo(&ed->history)) {
			cJSON *cur = snapshot_state(ed);
			cJSON *next = history_redo(&ed->history, cur);

			if (next) {
				restore_state(ed, next);
				cJSON_Delete(next);
				if (ed->undo_count > 0)
					ed->undo_count--;
				ed->render.dirty = 1;
			} else {
				cJSON_Delete(cur);
			}
		}
		return;
	case TB_KEY_CTRL_C:
	case TB_KEY_CTRL_Q:
		ed->running = 0;
		return;
	case TB_KEY_ESC:
		if (ed->mode != MODE_VIEW) {
			ed->mode = MODE_VIEW;
			ed->arrows.drag_state = ARROW_DRAG_NONE;
			ed->render.dirty = 1;
		}
		return;
	case TB_KEY_TAB:
		if (ed->bboxes.count > 0)
			bbox_select_next(&ed->bboxes);
		else if (ed->arrows.count > 0)
			arrow_select_next(&ed->arrows);
		ed->render.dirty = 1;
		return;
	case TB_KEY_ENTER:
		if (ed->mode == MODE_CONFIRM)
			ed->mode = MODE_VIEW;
		ed->render.dirty = 1;
		return;
	}

	if (ev->ch) {
		switch (ev->ch) {
		case 'q':
			ed->running = 0;
			break;
		case 'a': {
			int bw, bh, bx, by;

			if (ed->image_count <= 0)
				break;
			editor_push_history(ed, OP_ADD_BBOX);
			bw = ed->images[0].img.width / 4;
			bh = ed->images[0].img.height / 4;
			bx = ed->images[0].img.width / 2 - bw / 2;
			by = ed->images[0].img.height / 2 - bh / 2;
			bbox_add(&ed->bboxes, bx, by, bw, bh, 0,
				 NULL, 0);
			ed->mode = MODE_SELECT;
			ed->render.dirty = 1;
			break;
		}
		case 's':
			editor_save(ed);
			break;
		case 'x':
			ed->mode = MODE_ARROW_DRAW;
			ed->arrows.drag_state = ARROW_DRAG_NONE;
			ed->render.dirty = 1;
			break;
		case 'd': {
			struct arrow *as =
				arrow_get_selected(&ed->arrows);
			if (as) {
				editor_push_history(ed,
						    OP_REMOVE_ARROW);
				arrow_remove(&ed->arrows, as->id);
				ed->render.dirty = 1;
				break;
			}
			{
				struct bbox *sel =
					bbox_get_selected(&ed->bboxes);
				if (sel) {
					editor_push_history(ed,
							    OP_REMOVE_BBOX);
					bbox_remove(&ed->bboxes,
						    sel->id);
					ed->render.dirty = 1;
				}
			}
			break;
		}
		case 'u':
			if (history_can_undo(&ed->history)) {
				cJSON *cur = snapshot_state(ed);
				cJSON *prev;

				prev = history_undo(&ed->history, cur);
				if (prev) {
					restore_state(ed, prev);
					cJSON_Delete(prev);
					ed->undo_count++;
					ed->render.dirty = 1;
				} else {
					cJSON_Delete(cur);
				}
			}
			break;
		case 'r':
			if (history_can_redo(&ed->history)) {
				cJSON *cur = snapshot_state(ed);
				cJSON *next;

				next = history_redo(&ed->history, cur);
				if (next) {
					restore_state(ed, next);
					cJSON_Delete(next);
					if (ed->undo_count > 0)
						ed->undo_count--;
					ed->render.dirty = 1;
				} else {
					cJSON_Delete(cur);
				}
			}
			break;
		}
	}
}

int editor_run(struct editor *ed)
{
	int ret;

	ed->proto = terminal_detect();
	if (ed->proto == TERM_PROTO_UNKNOWN) {
		fprintf(stderr,
			"error: terminal does not support Sixel, Kitty, "
			"or iTerm2 graphics protocol\n");
		return -1;
	}

	ret = tb_init();
	if (ret < 0) {
		fprintf(stderr, "error: cannot initialize terminal\n");
		return -1;
	}

	tb_set_input_mode(TB_INPUT_ESC | TB_INPUT_MOUSE);
	tb_set_output_mode(TB_OUTPUT_256);

	tb_clear();
	tb_present();

	ed->running = 1;
	ed->render.dirty = 1;
	ed->render.proto = ed->proto;

	fprintf(stderr, "[morph] proto=%s\n",
		terminal_proto_name(ed->proto));

	while (ed->running) {
		struct tb_event ev;
		int nev;
		long long tnow;
		int wait_ms;

		wait_ms = (ed->mode == MODE_LABEL_EDIT) ? -1 : 30;

		for (nev = 0; nev < 64; nev++) {
			ret = tb_peek_event(&ev,
					    nev == 0 ? wait_ms : 0);
			if (ret != TB_OK)
				break;
			editor_handle_event(ed, &ev);
			if (!ed->running)
				break;
		}

		tnow = now_ms();

		if (ed->render.dirty) {
			if (ed->mode == MODE_LABEL_EDIT) {
				editor_redraw_status_bar_fast(ed);
				ed->last_render_ms = tnow;
			} else {
				editor_do_render(ed);
				ed->last_render_ms = tnow;
			}
		}
	}

	tb_shutdown();
	editor_dump_on_quit(ed);
	return 0;
}

void editor_stop(struct editor *ed)
{
	ed->running = 0;
}

void editor_render(struct editor *ed)
{
	ed->render.dirty = 1;
}
