#include "editor.h"
#include "render.h"
#include "json_util.h"
#include "canvas.h"
#include "draw.h"
#include "util.h"

#include "termbox2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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

static void editor_set_status(struct editor *ed, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(ed->status_msg, sizeof(ed->status_msg), fmt, ap);
	va_end(ap);
	ed->status_until_ms = now_ms() + 3000;
}

/*
 * The editor has two independent managers with their own selected
 * index. Every selection change must go through these helpers so
 * that at most one of them is active. Otherwise d/e/tab can act on
 * an object the user selected several actions ago.
 */
static struct bbox *editor_selected_bbox(struct editor *ed)
{
	if (ed->bboxes.selected < 0 ||
	    ed->bboxes.selected >= ed->bboxes.count)
		return NULL;
	return &ed->bboxes.boxes[ed->bboxes.selected];
}

static struct arrow *editor_selected_arrow(struct editor *ed)
{
	if (ed->arrows.selected < 0 ||
	    ed->arrows.selected >= ed->arrows.count)
		return NULL;
	return &ed->arrows.arrows[ed->arrows.selected];
}

static void editor_select_bbox(struct editor *ed, int idx)
{
	ed->bboxes.selected = idx;
	ed->arrows.selected = -1;
	if (idx >= 0 && idx < ed->bboxes.count)
		ed->active_image = ed->bboxes.boxes[idx].image_index;
}

static void editor_select_arrow(struct editor *ed, int idx)
{
	ed->arrows.selected = idx;
	ed->bboxes.selected = -1;
	if (idx >= 0 && idx < ed->arrows.count)
		ed->active_image = ed->arrows.arrows[idx].from.image_index;
}

static void editor_clear_selection(struct editor *ed)
{
	ed->bboxes.selected = -1;
	ed->arrows.selected = -1;
}

/*
 * Tab cycles through every annotation, not just bboxes. Bboxes come
 * first, then arrows, and the cycle wraps.
 */
static void editor_select_next(struct editor *ed)
{
	int bc = ed->bboxes.count;
	int ac = ed->arrows.count;

	if (bc == 0 && ac == 0) {
		editor_clear_selection(ed);
		return;
	}

	if (ed->arrows.selected < 0) {
		if (bc > 0 && ed->bboxes.selected + 1 < bc) {
			editor_select_bbox(ed, ed->bboxes.selected + 1);
			return;
		}
		if (ac > 0) {
			editor_select_arrow(ed, 0);
			return;
		}
		editor_select_bbox(ed, 0);
		return;
	}

	if (ed->arrows.selected + 1 < ac) {
		editor_select_arrow(ed, ed->arrows.selected + 1);
		return;
	}
	if (bc > 0)
		editor_select_bbox(ed, 0);
	else
		editor_select_arrow(ed, 0);
}

static void editor_clamp_bbox(struct editor *ed, struct bbox *b)
{
	int iw, ih;

	if (!b || b->image_index < 0 || b->image_index >= ed->image_count)
		return;
	iw = ed->images[b->image_index].img.width;
	ih = ed->images[b->image_index].img.height;
	if (iw <= 0 || ih <= 0)
		return;
	if (b->w < 1)
		b->w = 1;
	if (b->h < 1)
		b->h = 1;
	if (b->x < 0)
		b->x = 0;
	if (b->y < 0)
		b->y = 0;
	if (b->x >= iw)
		b->x = iw - 1;
	if (b->y >= ih)
		b->y = ih - 1;
	if (b->x + b->w > iw)
		b->w = iw - b->x;
	if (b->y + b->h > ih)
		b->h = ih - b->y;
}

static void format_label_line(struct editor *ed, int is_arrow, int id,
			      const char *label, char *buf, size_t n)
{
	int cur;

	if (ed->mode == MODE_LABEL_EDIT && ed->label_is_arrow == is_arrow &&
	    ed->label_target_id == id) {
		cur = ed->label_cursor;
		if (cur < 0)
			cur = 0;
		if (cur > (int)strlen(label))
			cur = (int)strlen(label);
		snprintf(buf, n, "  L: %.*s|%s", cur, label, label + cur);
	} else {
		snprintf(buf, n, "  L: %s", label);
	}
}

static void editor_draw_sidebar(struct editor *ed)
{
	int term_w, term_h;
	int sx, y, i, row;
	int skip;
	char buf[256];

	ed->sidebar_row_count = 0;
	skip = ed->sidebar_scroll;
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

			if (skip > 0) {
				skip--;
				continue;
			}

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
			format_label_line(ed, 0, b->id, label, buf,
					  sizeof(buf));
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

			if (skip > 0) {
				skip--;
				continue;
			}

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
			format_label_line(ed, 1, a->id, label, buf,
					  sizeof(buf));
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

	/*
	 * Arrow shafts are already drawn directly into the composited
	 * image pixel buffer (see composite + draw_arrow calls in
	 * editor_render), rendered as true solid lines by the terminal
	 * image protocol. This function only handles auxiliary elements
	 * on the character layer: label text and drag-start indicators.
	 */
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

static void editor_draw_help(struct editor *ed)
{
	static const char *lines[] = {
		"L-drag: new bbox        R-drag: select/move/resize",
		"Tab: cycle selection    a: add centered bbox",
		"e: edit label           d: delete    c: duplicate",
		"x: new arrow            j: edit arrow endpoints",
		"Arrows: nudge 1px       Shift+Arrows: nudge 10px",
		"Alt+Arrows: resize      Alt+Shift: resize 10px",
		"u / r: undo / redo      s: save annotations",
		"Video: Left/Right step  [ / ]: +-30 frames",
		"? or Esc: close help    q: quit"
	};
	int w, h, bw, bh, x, y, i, j;
	int n;

	(void)ed;
	w = tb_width();
	h = tb_height();
	if (w < 20 || h < 8)
		return;
	bw = 60;
	bh = 14;
	if (bw > w - 2)
		bw = w - 2;
	if (bh > h - 2)
		bh = h - 2;
	x = (w - bw) / 2;
	y = (h - bh) / 2;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	for (i = 0; i < bh; i++)
		for (j = 0; j < bw; j++)
			tb_set_cell(x + j, y + i, ' ', TB_WHITE, TB_DEFAULT);
	for (j = 0; j < bw; j++) {
		tb_set_cell(x + j, y, '-', TB_YELLOW, TB_DEFAULT);
		tb_set_cell(x + j, y + bh - 1, '-', TB_YELLOW, TB_DEFAULT);
	}
	for (i = 0; i < bh; i++) {
		tb_set_cell(x, y + i, '|', TB_YELLOW, TB_DEFAULT);
		tb_set_cell(x + bw - 1, y + i, '|', TB_YELLOW, TB_DEFAULT);
	}
	tb_print(x + 2, y + 1, TB_YELLOW | TB_BOLD, TB_DEFAULT,
		 "morph-editor keys");
	n = (int)(sizeof(lines) / sizeof(lines[0]));
	for (i = 0; i < n && i + 3 < bh - 1; i++)
		tb_print(x + 2, y + 3 + i, TB_WHITE, TB_DEFAULT,
			 lines[i]);
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
	} else if (ed->is_video) {
		snprintf(line2, sizeof(line2),
			 " video frame %d/%d (fps=%.2f) [</>]step [[/]] +-30 [a]dd [d]el [e]label [s]ave [q]uit",
			 ed->video.current_frame,
			 ed->video.total_frames,
			 ed->video.fps);
	} else {
		snprintf(line2, sizeof(line2),
			 " L=draw R=select [a]dd [d]el [e]label [x]arrow [s]ave [u]ndo [r]edo [q]uit");
	}
	if (ed->status_msg[0] && now_ms() < ed->status_until_ms)
		snprintf(line2, sizeof(line2), " %s", ed->status_msg);
	tb_print(0, img_rows + 2, TB_WHITE, TB_DEFAULT, line2);

	editor_draw_sidebar(ed);
	editor_draw_bbox_overlay(ed);
	editor_draw_arrow_overlay(ed);
	if (ed->show_help)
		editor_draw_help(ed);
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
	} else if (ed->is_video) {
		snprintf(line2, sizeof(line2),
			 " video frame %d/%d (fps=%.2f) [</>]step [[/]] +-30 [a]dd [d]el [e]label [s]ave [q]uit",
			 ed->video.current_frame,
			 ed->video.total_frames,
			 ed->video.fps);
	} else {
		snprintf(line2, sizeof(line2),
			 " L=draw R=select [a]dd [d]el [e]label [x]arrow [s]ave [u]ndo [r]edo [q]uit");
	}
	if (ed->status_msg[0] && now_ms() < ed->status_until_ms)
		snprintf(line2, sizeof(line2), " %s", ed->status_msg);
	tb_print(0, img_rows + 2, TB_WHITE, TB_DEFAULT, line2);

	editor_draw_sidebar(ed);

	{
		unsigned long arrows_fp = 0;
		int need_image_upload;

		/*
		 * Arrows are drawn directly into image pixels, so arrow
		 * data changes must also trigger re-compositing +
		 * re-upload. The fingerprint simply accumulates each
		 * arrow's endpoints and color into a 64-bit rolling hash.
		 */
		{
			int ai;
			for (ai = 0; ai < ed->arrows.count; ai++) {
				struct arrow *a = &ed->arrows.arrows[ai];
				arrows_fp = arrows_fp * 1315423911u
					+ (unsigned long)(a->from.image_index
							  * 73856093);
				arrows_fp = arrows_fp * 1315423911u
					+ (unsigned long)(a->from.x * 19349663);
				arrows_fp = arrows_fp * 1315423911u
					+ (unsigned long)(a->from.y * 83492791);
				arrows_fp = arrows_fp * 1315423911u
					+ (unsigned long)(a->to.image_index
							  * 12582917);
				arrows_fp = arrows_fp * 1315423911u
					+ (unsigned long)(a->to.x * 25165843);
				arrows_fp = arrows_fp * 1315423911u
					+ (unsigned long)(a->to.y * 50331653);
				arrows_fp = arrows_fp * 1315423911u
					+ (unsigned long)a->color;
			}
			/*
			 * Note: ed->arrows.selected is intentionally
			 * excluded from the fingerprint. Otherwise every
			 * sidebar click to switch the selected arrow would
			 * trigger a full multi-image re-composite +
			 * re-upload, which is very laggy. Selection state
			 * is indicated by sidebar highlighting alone.
			 */
		}

		need_image_upload =
			(!ed->image_uploaded) ||
			(ed->image_gen != ed->last_uploaded_gen) ||
			(term_w != ed->last_term_w) ||
			(term_h != ed->last_term_h) ||
			(arrows_fp != ed->last_arrows_fp);

		/*
		 * Only enter sync compositing mode when image re-upload
		 * is actually needed. \033[?2026h..l atomically commits
		 * "image re-upload + overlay + tb_present" to avoid
		 * flicker. When only the overlay changes (e.g. sidebar
		 * toggle, bbox selection), skip sync mode; otherwise each
		 * click adds an extra tb_present, causing noticeable lag.
		 */
		if (need_image_upload) {
			tb_send("\033[?2026h", 8);
			tb_present();
			tb_send("\033[H", 3);

			if (ed->image_count == 1) {
				struct image_slot *sl = &ed->images[0];
				char _b[64];
				int _n;
				char *img_data = NULL;
				unsigned char *src_px = sl->img.pixels;
				int src_w = sl->img.width;
				int src_h = sl->img.height;
				int src_ch = sl->img.channels;

				_n = snprintf(_b, sizeof(_b),
					"\033[%d;%dH",
					sl->canvas_y + 1,
					sl->canvas_x + 1);
				tb_send(_b, _n);

				/*
				 * Draw arrows directly into a copy of the
				 * image pixels, rendered by the terminal
				 * image protocol (kitty/iterm2/sixel) as
				 * a regular image — so arrows appear as
				 * true pixel solid lines, not character
				 * approximations.
				 */
				if (ed->arrows.count > 0) {
					size_t sz = (size_t)src_w * src_h
						* src_ch;
					unsigned char *copy = arena_alloc(
						&ed->arenas.arenas[
							ARENA_COMMAND], sz);
					if (copy) {
						int ai;
						int thick = src_w / 400;

						if (thick < 2)
							thick = 2;
						memcpy(copy, src_px, sz);
						for (ai = 0;
						     ai < ed->arrows.count;
						     ai++) {
							struct arrow *a =
								&ed->arrows
								.arrows[ai];
							uint32_t color =
								a->color
								? a->color
								: 0xff3030u;
							if (a->from.image_index
							    != 0 ||
							    a->to.image_index
							    != 0)
								continue;
							draw_arrow(
								copy,
								src_w, src_h,
								src_ch,
								a->from.x,
								a->from.y,
								a->to.x,
								a->to.y,
								color,
								thick);
						}
						src_px = copy;
					}
				}

				if (ed->proto == TERM_PROTO_KITTY) {
					int dc = sl->display_w;
					int dr = (sl->display_h + 1) / 2;
					int image_id =
						ed->render.kitty_image_base;
					if (dc < 1) dc = 1;
					if (dr < 1) dr = 1;
					/*
					 * Delete the previous placement
					 * before transmitting a new image
					 * with the same stable id.
					 */
					kitty_delete_image(image_id);
					img_data = render_image_kitty(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						src_px,
						src_w,
						src_h,
						src_ch,
						image_id,
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
						src_px,
						src_w,
						src_h,
						src_ch,
						dc, dr);
				} else {
					unsigned char *dp;
					dp = image_resize(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						src_px,
						src_w,
						src_h,
						src_ch,
						sl->display_w,
						sl->display_h);
					if (dp) {
						img_data =
							render_image_sixel(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						dp, sl->display_w,
						sl->display_h,
						src_ch);
					}
				}
				if (img_data)
					tb_send(img_data,
						strlen(img_data));
			} else {
				/*
				 * Cell-to-pixel scale: layout gives
				 * display_w / canvas_x in "terminal columns"
				 * and display_h / canvas_y*2 in "half-cells".
				 * Using them directly as pixels yields ~1
				 * pixel/cell, which looks blurry when the
				 * terminal scales it up. Here we composite
				 * at SUPER× in pixel space, then let the
				 * terminal protocol render at the original
				 * cell size, producing a high-resolution
				 * downsampled result.
				 */
				const int SUPER = 8;
				int cw_cells = 0, ch_halfcells = 0;
				int cw_px, ch_px;
				int disp_cols, disp_rows;
				struct canvas_layer layers[MAX_IMAGES];
				unsigned char *composite_pixels;
				char *img_data = NULL;

				for (i = 0; i < ed->image_count; i++) {
					int r;
					r = ed->layout_slots[i].canvas_y +
						(ed->layout_slots[i].display_h + 1) / 2;
					if (r > ch_halfcells)
						ch_halfcells = r;
					r = ed->layout_slots[i].canvas_x +
						ed->layout_slots[i].display_w;
					if (r > cw_cells)
						cw_cells = r;
				}
				disp_cols = cw_cells;
				disp_rows = ch_halfcells;
				cw_px = cw_cells * SUPER;
				ch_px = ch_halfcells * 2 * SUPER;

				for (i = 0; i < ed->image_count; i++) {
					struct image_slot *sl = &ed->images[i];
					unsigned char *resized;
					int rw = sl->display_w * SUPER;
					int rh = sl->display_h * SUPER;

					memset(&layers[i], 0,
					       sizeof(layers[i]));
					if (rw < 1) rw = 1;
					if (rh < 1) rh = 1;
					resized = image_resize(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						sl->img.pixels,
						sl->img.width,
						sl->img.height,
						sl->img.channels,
						rw, rh);
					if (resized) {
						layers[i].pixels = resized;
						layers[i].w = rw;
						layers[i].h = rh;
						layers[i].x =
						  ed->layout_slots[i]
						  	.canvas_x * SUPER;
						layers[i].y =
						  ed->layout_slots[i]
						  	.canvas_y * 2 * SUPER;
						layers[i].opacity = 1.0f;
						layers[i].blend =
							BLEND_NORMAL;
					}
				}

				composite_pixels = canvas_composite(
					&ed->arenas.arenas[ARENA_COMMAND],
					cw_px, ch_px, layers,
					ed->image_count, 0x1a1a2e);

				/*
				 * Draw arrows directly on composited pixels,
				 * so the terminal image protocol renders them
				 * as pixel graphics — true solid lines, not
				 * character approximations.
				 *
				 * Pixel coordinate mapping: image (px, py)
				 * after resize to (display_w*SUPER,
				 * display_h*SUPER), plus layout offsets
				 * canvas_x*SUPER, canvas_y*2*SUPER.
				 */
				if (composite_pixels &&
				    ed->arrows.count > 0) {
					int ai;
					int thick = cw_px / 400;

					if (thick < 2)
						thick = 2;
					for (ai = 0;
					     ai < ed->arrows.count;
					     ai++) {
						struct arrow *a =
							&ed->arrows.arrows[ai];
						struct image_slot *sf;
						struct image_slot *st;
						int fx, fy, tx2, ty2;
						uint32_t color;

						if (a->from.image_index < 0 ||
						    a->from.image_index >=
						    ed->image_count)
							continue;
						if (a->to.image_index < 0 ||
						    a->to.image_index >=
						    ed->image_count)
							continue;
						sf = &ed->images[
							a->from.image_index];
						st = &ed->images[
							a->to.image_index];
						if (sf->img.width <= 0 ||
						    sf->img.height <= 0 ||
						    st->img.width <= 0 ||
						    st->img.height <= 0)
							continue;
						fx = ed->layout_slots[
							a->from.image_index]
							.canvas_x * SUPER
							+ (int)((long long)
								a->from.x
								* sf->display_w
								* SUPER
								/ sf->img.width);
						fy = ed->layout_slots[
							a->from.image_index]
							.canvas_y * 2 * SUPER
							+ (int)((long long)
								a->from.y
								* sf->display_h
								* SUPER
								/ sf->img.height);
						tx2 = ed->layout_slots[
							a->to.image_index]
							.canvas_x * SUPER
							+ (int)((long long)
								a->to.x
								* st->display_w
								* SUPER
								/ st->img.width);
						ty2 = ed->layout_slots[
							a->to.image_index]
							.canvas_y * 2 * SUPER
							+ (int)((long long)
								a->to.y
								* st->display_h
								* SUPER
								/ st->img.height);
						color = a->color
							? a->color
							: 0xff3030u;
						draw_arrow(composite_pixels,
							   cw_px, ch_px, 4,
							   fx, fy,
							   tx2, ty2,
							   color, thick);
					}
				}

				if (composite_pixels) {
					if (ed->proto ==
					    TERM_PROTO_KITTY) {
						int image_id =
						  ed->render.kitty_image_base;
						kitty_delete_image(image_id);
						img_data =
						  render_image_kitty(
							&ed->arenas.arenas[
								ARENA_COMMAND],
							composite_pixels,
							cw_px, ch_px, 4,
							image_id,
							ed->render
								.kitty_placement_id,
							disp_cols, disp_rows);
					} else if (ed->proto ==
						   TERM_PROTO_ITERM2) {
						img_data =
						  render_image_iterm2(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						composite_pixels,
						cw_px, ch_px, 4,
						disp_cols, disp_rows);
					} else {
						img_data =
						  render_image_sixel(
						&ed->arenas.arenas[
							ARENA_COMMAND],
						composite_pixels,
						cw_px, ch_px, 4);
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
			ed->last_arrows_fp = arrows_fp;
		}

	editor_draw_bbox_overlay(ed);
	editor_draw_arrow_overlay(ed);
	if (ed->show_help)
		editor_draw_help(ed);
	tb_present();
		if (need_image_upload) {
			/* Sync compositing END: image + overlay visible at once */
			tb_send("\033[?2026l", 8);
		}
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
	ed->forced_proto = -1;
	ed->active_image = 0;
	return 0;
}

void editor_free(struct editor *ed)
{
	int i;

	if (ed->running)
		editor_stop(ed);
	if (ed->is_video) {
		video_close(&ed->video);
		ed->is_video = 0;
	}
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

/*
 * path_is_video - check filename extension for known video formats
 */
int path_is_video(const char *path)
{
	const char *ext;
	const char *exts[] = {
		".mp4", ".MP4", ".mov", ".MOV", ".mkv", ".MKV",
		".avi", ".AVI", ".webm", ".WEBM", ".m4v", ".M4V",
		".flv", ".FLV", ".wmv", ".WMV", ".mpeg", ".MPEG",
		".mpg", ".MPG", ".ts", ".TS"
	};
	size_t i;

	if (!path)
		return 0;
	ext = strrchr(path, '.');
	if (!ext)
		return 0;
	for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
		if (strcmp(ext, exts[i]) == 0)
			return 1;
	}
	return 0;
}

int editor_set_proto(struct editor *ed, const char *name)
{
	if (!name || strcmp(name, "auto") == 0) {
		ed->forced_proto = -1;
		return 0;
	}
	if (strcmp(name, "kitty") == 0)
		ed->forced_proto = TERM_PROTO_KITTY;
	else if (strcmp(name, "sixel") == 0)
		ed->forced_proto = TERM_PROTO_SIXEL;
	else if (strcmp(name, "iterm2") == 0 ||
		 strcmp(name, "iterm") == 0)
		ed->forced_proto = TERM_PROTO_ITERM2;
	else
		return -1;
	return 0;
}

static int editor_video_load_sync(struct editor *ed, int frame_num)
{
	unsigned char *px;
	int w, h;
	struct image *img;

	if (!ed->is_video)
		return -1;
	if (frame_num < 0)
		frame_num = 0;
	if (ed->video.total_frames > 0 &&
	    frame_num >= ed->video.total_frames)
		frame_num = ed->video.total_frames - 1;

	arena_reset(&ed->arenas.arenas[ARENA_IMAGE]);
	px = video_get_frame(&ed->arenas.arenas[ARENA_IMAGE],
			     &ed->video, frame_num, &w, &h);
	if (!px)
		return -1;

	img = &ed->images[0].img;
	img->pixels = px;
	img->width = w;
	img->height = h;
	img->channels = 4;

	ed->video.current_frame = frame_num;
	ed->image_count = 1;
	ed->image_gen++;
	ed->image_uploaded = 0;
	ed->render.dirty = 1;

	bbox_apply_frame(&ed->bboxes, frame_num);
	arrow_apply_frame(&ed->arrows, frame_num);
	return 0;
}

int editor_video_seek(struct editor *ed, int frame_num)
{
	if (!ed->is_video)
		return -1;
	if (frame_num < 0)
		frame_num = 0;
	if (ed->video.total_frames > 0 &&
	    frame_num >= ed->video.total_frames)
		frame_num = ed->video.total_frames - 1;
	if (frame_num == ed->video.current_frame)
		return 0;

	ed->video.current_frame = frame_num;
	bbox_apply_frame(&ed->bboxes, frame_num);
	arrow_apply_frame(&ed->arrows, frame_num);
	video_request_frame(&ed->video, frame_num);
	editor_set_status(ed, "loading frame %d/%d", frame_num,
			  ed->video.total_frames);
	ed->render.dirty = 1;
	return 0;
}

int editor_video_poll(struct editor *ed)
{
	unsigned char *px;
	int frame, w, h;
	struct image *img;
	size_t sz;

	if (!ed->is_video)
		return 0;
	if (!video_take_loaded(&ed->video, &frame, &px, &w, &h))
		return 0;
	if (frame == ed->video.current_frame) {
		sz = (size_t)w * h * 4;
		arena_reset(&ed->arenas.arenas[ARENA_IMAGE]);
		img = &ed->images[0].img;
		img->pixels = arena_alloc(
			&ed->arenas.arenas[ARENA_IMAGE], sz);
		if (img->pixels) {
			memcpy(img->pixels, px, sz);
			img->width = w;
			img->height = h;
			img->channels = 4;
			ed->image_gen++;
			ed->image_uploaded = 0;
			ed->render.dirty = 1;
			editor_set_status(ed, "frame %d/%d", frame,
					  ed->video.total_frames);
		}
	}
	free(px);
	return 1;
}

int editor_open_video(struct editor *ed, const char *path)
{
	int ret;

	if (ed->is_video) {
		video_close(&ed->video);
		ed->is_video = 0;
	}

	arena_reset(&ed->arenas.arenas[ARENA_IMAGE]);
	arena_reset(&ed->arenas.arenas[ARENA_FRAME]);

	ret = video_open(&ed->arenas.arenas[ARENA_SESSION], path,
			 &ed->video);
	if (ret < 0)
		return ret;

	strncpy(ed->images[0].img.path, path,
		sizeof(ed->images[0].img.path) - 1);
	ed->images[0].img.path[
		sizeof(ed->images[0].img.path) - 1] = '\0';
	ed->is_video = 1;
	ed->image_count = 1;

	if (editor_video_load_sync(ed, 0) < 0) {
		video_close(&ed->video);
		ed->is_video = 0;
		ed->image_count = 0;
		return -1;
	}
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

	arr = cJSON_GetObjectItem(snap, "bboxes");
	if (!arr)
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

	/*
	 * Snapshots written by older versions could have both
	 * selections set. Restore a single active selection.
	 */
	tmp = cJSON_GetObjectItem(snap, "selection");
	if (tmp && cJSON_IsNumber(tmp)) {
		if (tmp->valueint == 1) {
			ed->arrows.selected = -1;
		} else if (tmp->valueint == 2) {
			ed->bboxes.selected = -1;
		} else if (tmp->valueint == 0) {
			editor_clear_selection(ed);
		}
	} else if (ed->bboxes.selected >= 0) {
		ed->arrows.selected = -1;
	} else if (ed->arrows.selected >= 0) {
		ed->bboxes.selected = -1;
	}
	if (ed->bboxes.selected >= ed->bboxes.count)
		ed->bboxes.selected = ed->bboxes.count - 1;
	if (ed->arrows.selected >= ed->arrows.count)
		ed->arrows.selected = ed->arrows.count - 1;
}

static char *editor_dump_json(struct editor *ed)
{
	cJSON *root;
	cJSON *arr;
	char *result;
	int i;

	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "version", 1);
	cJSON_AddNumberToObject(root, "selection",
				ed->arrows.selected >= 0 ? 2 :
				(ed->bboxes.selected >= 0 ? 1 : 0));

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

	if (ed->is_video) {
		cJSON *video;

		video = cJSON_CreateObject();
		cJSON_AddStringToObject(video, "path",
					ed->images[0].img.path);
		cJSON_AddNumberToObject(video, "total_frames",
					ed->video.total_frames);
		cJSON_AddNumberToObject(video, "fps", ed->video.fps);
		cJSON_AddNumberToObject(video, "frame",
					ed->video.current_frame);
		cJSON_AddItemToObject(root, "video", video);
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

static char *editor_read_file(const char *path)
{
	FILE *fp;
	long sz;
	char *buf;

	fp = fopen(path, "rb");
	if (!fp)
		return NULL;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return NULL;
	}
	sz = ftell(fp);
	if (sz < 0) {
		fclose(fp);
		return NULL;
	}
	rewind(fp);
	buf = malloc((size_t)sz + 1);
	if (buf) {
		if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
			free(buf);
			buf = NULL;
		} else {
			buf[sz] = '\0';
		}
	}
	fclose(fp);
	return buf;
}

void editor_set_annotation_path(struct editor *ed, const char *path)
{
	ed->annotation_path = path;
}

int editor_save_annotations(struct editor *ed, const char *path)
{
	char *result;
	char *tmp;
	FILE *fp;
	size_t len;
	int ok;

	if (!path) {
		editor_set_status(ed, "no annotation file (use --annotations)");
		return -1;
	}

	result = editor_dump_json(ed);
	if (!result)
		return -1;
	tmp = malloc(strlen(path) + 5);
	if (!tmp) {
		free(result);
		return -1;
	}
	snprintf(tmp, strlen(path) + 5, "%s.tmp", path);

	ok = 0;
	fp = fopen(tmp, "wb");
	if (fp) {
		len = strlen(result);
		if (fwrite(result, 1, len, fp) == len &&
		    fputc('\n', fp) != EOF && fflush(fp) == 0) {
			if (fclose(fp) == 0 && rename(tmp, path) == 0)
				ok = 1;
			fp = NULL;
		}
		if (fp)
			fclose(fp);
	}

	if (!ok) {
		remove(tmp);
		editor_set_status(ed, "error: cannot save %s", path);
	} else {
		ed->dirty_annotations = 0;
		editor_set_status(ed, "saved %d bboxes, %d arrows",
				  ed->bboxes.count, ed->arrows.count);
	}
	free(tmp);
	free(result);
	return ok ? 0 : -1;
}

static void editor_save(struct editor *ed)
{
	char *result;

	if (ed->annotation_path) {
		editor_save_annotations(ed, ed->annotation_path);
		return;
	}
	result = editor_dump_json(ed);
	if (result) {
		fprintf(stderr, "%s\n", result);
		free(result);
		editor_set_status(ed,
				  "printed JSON to stderr (use --annotations)");
	}
}

int editor_load_annotations(struct editor *ed, const char *path)
{
	char *text;
	cJSON *root;
	cJSON *video;
	cJSON *frame;
	int frame_num;

	if (!path)
		return -1;
	text = editor_read_file(path);
	if (!text) {
		editor_set_status(ed, "annotations: %s (new file)", path);
		return 0;
	}
	root = cJSON_Parse(text);
	free(text);
	if (!root) {
		editor_set_status(ed, "error: invalid annotations JSON");
		return -1;
	}

	restore_state(ed, root);
	frame_num = -1;
	video = cJSON_GetObjectItem(root, "video");
	if (video) {
		frame = cJSON_GetObjectItem(video, "frame");
		if (frame && cJSON_IsNumber(frame))
			frame_num = frame->valueint;
	}
	cJSON_Delete(root);

	if (ed->is_video && frame_num >= 0)
		editor_video_seek(ed, frame_num);
	ed->dirty_annotations = 0;
	editor_set_status(ed, "loaded %d bboxes, %d arrows",
			  ed->bboxes.count, ed->arrows.count);
	return 0;
}

static void editor_txn_begin(struct editor *ed)
{
	if (!ed->pending_snapshot)
		ed->pending_snapshot = snapshot_state(ed);
}

static void editor_txn_commit(struct editor *ed)
{
	cJSON *snap;

	snap = ed->pending_snapshot;
	if (!snap)
		return;
	ed->pending_snapshot = NULL;
	if (history_push(&ed->history, OP_EDIT, snap) < 0)
		cJSON_Delete(snap);
	ed->undo_count++;
	ed->dirty_annotations = 1;
	ed->render.dirty = 1;
	if (ed->annotation_path)
		editor_save_annotations(ed, ed->annotation_path);
}

static void editor_txn_abort(struct editor *ed)
{
	if (ed->pending_snapshot) {
		cJSON_Delete(ed->pending_snapshot);
		ed->pending_snapshot = NULL;
	}
}

static void editor_dump_on_quit(struct editor *ed)
{
	char *result;

	editor_txn_commit(ed);
	if (ed->annotation_path) {
		if (ed->dirty_annotations)
			editor_save_annotations(ed, ed->annotation_path);
		return;
	}
	result = editor_dump_json(ed);
	if (result) {
		fprintf(stdout, "%s\n", result);
		fflush(stdout);
		free(result);
	}
}
static void editor_enter_label_edit(struct editor *ed, int target_id,
				    int is_arrow, const char *initial,
				    int created)
{
	ed->mode = MODE_LABEL_EDIT;
	ed->label_target_id = target_id;
	ed->label_is_arrow = is_arrow;
	ed->label_created = created;
	ed->label_dirty = created;
	if (initial) {
		strncpy(ed->label_buf, initial, ARROW_LABEL_MAX - 1);
		ed->label_buf[ARROW_LABEL_MAX - 1] = '\0';
		ed->label_len = (int)strlen(ed->label_buf);
	} else {
		ed->label_buf[0] = '\0';
		ed->label_len = 0;
	}
	ed->label_cursor = ed->label_len;
	strncpy(ed->label_original, ed->label_buf,
		ARROW_LABEL_MAX - 1);
	ed->label_original[ARROW_LABEL_MAX - 1] = '\0';
	ed->render.dirty = 1;
}

static void editor_commit_label(struct editor *ed)
{
	int changed;

	changed = ed->label_created ||
		  strcmp(ed->label_buf, ed->label_original) != 0;
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
	if (changed)
		editor_txn_commit(ed);
	else
		editor_txn_abort(ed);
	ed->mode = MODE_VIEW;
	ed->label_buf[0] = '\0';
	ed->label_len = 0;
	ed->label_cursor = 0;
	ed->label_dirty = 0;
	ed->label_created = 0;
	ed->label_target_id = -1;
	ed->render.dirty = 1;
}

static void editor_cancel_label(struct editor *ed)
{
	if (ed->label_created)
		editor_txn_commit(ed);
	else
		editor_txn_abort(ed);
	ed->mode = MODE_VIEW;
	ed->label_buf[0] = '\0';
	ed->label_len = 0;
	ed->label_cursor = 0;
	ed->label_dirty = 0;
	ed->label_created = 0;
	ed->label_target_id = -1;
	ed->render.dirty = 1;
}

static void label_handle_char(struct editor *ed, uint32_t ch)
{
	unsigned int cp = ch;
	char enc[4];
	int n = 0;

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
	if (ed->label_len + n >= ARROW_LABEL_MAX - 1)
		return;
	if (ed->label_cursor < 0 || ed->label_cursor > ed->label_len)
		ed->label_cursor = ed->label_len;
	memmove(ed->label_buf + ed->label_cursor + n,
		ed->label_buf + ed->label_cursor,
		(size_t)(ed->label_len - ed->label_cursor + 1));
	memcpy(ed->label_buf + ed->label_cursor, enc, (size_t)n);
	ed->label_len += n;
	ed->label_cursor += n;
	ed->label_dirty = 1;
	ed->render.dirty = 1;
}

static void label_handle_backspace(struct editor *ed)
{
	int start;
	int len;

	if (ed->label_cursor <= 0)
		return;
	start = ed->label_cursor - 1;
	while (start > 0 &&
	       ((unsigned char)ed->label_buf[start] & 0xC0) == 0x80)
		start--;
	len = ed->label_cursor - start;
	memmove(ed->label_buf + start, ed->label_buf + ed->label_cursor,
		(size_t)(ed->label_len - ed->label_cursor + 1));
	ed->label_len -= len;
	ed->label_cursor = start;
	ed->label_dirty = 1;
	ed->render.dirty = 1;
}

static void label_handle_delete(struct editor *ed)
{
	int end;
	int len;

	if (ed->label_cursor >= ed->label_len)
		return;
	end = ed->label_cursor + 1;
	while (end < ed->label_len &&
	       ((unsigned char)ed->label_buf[end] & 0xC0) == 0x80)
		end++;
	len = end - ed->label_cursor;
	memmove(ed->label_buf + ed->label_cursor,
		ed->label_buf + end,
		(size_t)(ed->label_len - end + 1));
	ed->label_len -= len;
	ed->render.dirty = 1;
}

static void label_cursor_left(struct editor *ed)
{
	if (ed->label_cursor <= 0)
		return;
	ed->label_cursor--;
	while (ed->label_cursor > 0 &&
	       ((unsigned char)ed->label_buf[ed->label_cursor] & 0xC0) ==
	       0x80)
		ed->label_cursor--;
	ed->render.dirty = 1;
}

static void label_cursor_right(struct editor *ed)
{
	if (ed->label_cursor >= ed->label_len)
		return;
	ed->label_cursor++;
	while (ed->label_cursor < ed->label_len &&
	       ((unsigned char)ed->label_buf[ed->label_cursor] & 0xC0) ==
	       0x80)
		ed->label_cursor++;
	ed->render.dirty = 1;
}

static void editor_delete_selected(struct editor *ed)
{
	struct arrow *a;
	struct bbox *b;

	a = editor_selected_arrow(ed);
	if (a) {
		editor_txn_begin(ed);
		if (ed->is_video && a->kf_count > 0) {
			arrow_remove_keyframe(&ed->arrows, a->id,
					      ed->video.current_frame);
			a = editor_selected_arrow(ed);
			if (a && a->kf_count == 0)
				arrow_remove(&ed->arrows, a->id);
		} else {
			arrow_remove(&ed->arrows, a->id);
		}
		editor_txn_commit(ed);
		ed->render.dirty = 1;
		return;
	}

	b = editor_selected_bbox(ed);
	if (!b)
		return;
	editor_txn_begin(ed);
	if (ed->is_video && b->kf_count > 0) {
		bbox_remove_keyframe(&ed->bboxes, b->id,
				     ed->video.current_frame);
		b = editor_selected_bbox(ed);
		if (b && b->kf_count == 0)
			bbox_remove(&ed->bboxes, b->id);
	} else {
		bbox_remove(&ed->bboxes, b->id);
	}
	editor_txn_commit(ed);
	ed->render.dirty = 1;
}

static void editor_duplicate_selected(struct editor *ed)
{
	struct bbox *b;
	struct arrow *a;
	int id;

	b = editor_selected_bbox(ed);
	if (b) {
		editor_txn_begin(ed);
		id = bbox_add(&ed->bboxes, b->x + 10, b->y + 10,
			      b->w, b->h, b->image_index,
			      b->label, b->color);
		if (id > 0) {
			struct bbox *nb = &ed->bboxes.boxes[
				ed->bboxes.count - 1];
			editor_clamp_bbox(ed, nb);
			editor_select_bbox(ed, ed->bboxes.count - 1);
			if (ed->is_video)
				bbox_set_keyframe(&ed->bboxes, id,
						  ed->video.current_frame,
						  nb->x, nb->y,
						  nb->w, nb->h);
			editor_txn_commit(ed);
		} else {
			editor_txn_abort(ed);
		}
		ed->render.dirty = 1;
		return;
	}

	a = editor_selected_arrow(ed);
	if (!a)
		return;
	editor_txn_begin(ed);
	id = arrow_add(&ed->arrows, a->from, a->to, a->label, a->color);
	if (id > 0) {
		editor_select_arrow(ed, ed->arrows.count - 1);
		if (ed->is_video)
			arrow_set_keyframe(&ed->arrows, id,
					   ed->video.current_frame,
					   a->from, a->to);
		editor_txn_commit(ed);
	} else {
		editor_txn_abort(ed);
	}
	ed->render.dirty = 1;
}

static void editor_nudge_selected(struct editor *ed, int dx, int dy,
				  int resize)
{
	struct bbox *b;
	struct arrow *a;

	b = editor_selected_bbox(ed);
	a = editor_selected_arrow(ed);
	if (!b && !a)
		return;

	editor_txn_begin(ed);
	if (b) {
		if (resize) {
			b->w += dx;
			b->h += dy;
			if (b->w < 1)
				b->w = 1;
			if (b->h < 1)
				b->h = 1;
		} else {
			b->x += dx;
			b->y += dy;
		}
		editor_clamp_bbox(ed, b);
		if (ed->is_video)
			bbox_set_keyframe(&ed->bboxes, b->id,
					  ed->video.current_frame,
					  b->x, b->y, b->w, b->h);
	} else if (a) {
		a->from.x += dx;
		a->from.y += dy;
		a->to.x += dx;
		a->to.y += dy;
		if (ed->is_video)
			arrow_set_keyframe(&ed->arrows, a->id,
					   ed->video.current_frame,
					   a->from, a->to);
	}
	editor_txn_commit(ed);
	ed->render.dirty = 1;
}

static void editor_edit_selected_label(struct editor *ed)
{
	struct arrow *a;
	struct bbox *b;

	a = editor_selected_arrow(ed);
	if (a) {
		editor_txn_begin(ed);
		editor_enter_label_edit(ed, a->id, 1, a->label, 0);
		return;
	}
	b = editor_selected_bbox(ed);
	if (b) {
		editor_txn_begin(ed);
		editor_enter_label_edit(ed, b->id, 0, b->label, 0);
	}
}

static void editor_edit_arrow_endpoints(struct editor *ed)
{
	struct arrow *a;

	a = editor_selected_arrow(ed);
	if (!a)
		return;
	ed->arrow_edit_id = a->id;
	ed->mode = MODE_ARROW_DRAW;
	ed->arrows.drag_state = ARROW_DRAG_NONE;
	editor_set_status(ed, "click new start, then new end");
	ed->render.dirty = 1;
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

			if (ev->key == TB_KEY_MOUSE_WHEEL_UP ||
			    ev->key == TB_KEY_MOUSE_WHEEL_DOWN) {
				if (sidebar_x >= 0 && ev->x > sidebar_x) {
					if (ev->key ==
					    TB_KEY_MOUSE_WHEEL_UP) {
						if (ed->sidebar_scroll > 0)
							ed->sidebar_scroll--;
					} else {
						ed->sidebar_scroll++;
					}
					ed->render.dirty = 1;
				}
				return;
			}

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
								editor_select_bbox(
									ed, k);
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
								editor_select_arrow(
									ed, k);
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
					int new_id;

					from = ed->arrows.drag_from;
					to.image_index = img_idx;
					to.x = px;
					to.y = py;

						editor_txn_begin(ed);
						if (ed->arrow_edit_id > 0) {
							struct arrow *ae = NULL;
							int ai;

							for (ai = 0;
							     ai < ed->arrows.count;
							     ai++) {
								if (ed->arrows.arrows[ai].id ==
								    ed->arrow_edit_id) {
									ae = &ed->arrows.arrows[ai];
									break;
								}
							}
							if (!ae) {
								editor_txn_abort(ed);
							} else {
								ae->from = from;
								ae->to = to;
								if (ed->is_video)
									arrow_set_keyframe(
										&ed->arrows,
										ae->id,
										ed->video.current_frame,
										from, to);
								editor_select_arrow(ed, ai);
								editor_txn_commit(ed);
							}
							ed->arrow_edit_id = 0;
							ed->arrows.drag_state =
								ARROW_DRAG_NONE;
							ed->mode = MODE_VIEW;
							ed->force_full_render = 1;
							ed->render.dirty = 1;
							return;
						}
						new_id = arrow_add(&ed->arrows,
								   from, to,
								   NULL, 0);
						if (new_id <= 0) {
							editor_txn_abort(ed);
						} else {
							if (ed->is_video)
								arrow_set_keyframe(
									&ed->arrows,
									new_id,
									ed->video.current_frame,
									from, to);
							editor_select_arrow(
								ed,
								ed->arrows.count - 1);
						}
						ed->arrows.drag_state =
							ARROW_DRAG_NONE;
						ed->mode = MODE_VIEW;

				/*
				 * Ensure the main loop runs editor_do_render
				 * after entering LABEL_EDIT, so the newly
				 * added arrow is immediately drawn onto the
				 * image.
				 */
					ed->force_full_render = 1;
					ed->render.dirty = 1;

						if (new_id > 0)
							editor_enter_label_edit(
								ed, new_id, 1,
								NULL, 1);
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
						int prev_state =
							ed->bboxes.drag_state;

						id = bbox_mouse_up(&ed->bboxes);
						if (id >= 0) {
							struct bbox *nb =
							  &ed->bboxes.boxes[
							    ed->bboxes.count - 1];
							nb->image_index =
							  ed->drag_image_index;
							editor_select_bbox(
							  ed,
							  ed->bboxes.count - 1);
							editor_clamp_bbox(ed, nb);
							if (ed->is_video)
								bbox_set_keyframe(
								  &ed->bboxes, id,
								  ed->video.current_frame,
								  nb->x, nb->y,
								  nb->w, nb->h);
							editor_enter_label_edit(
							  ed, id, 0, NULL, 1);
						} else {
							struct bbox *sel =
							  editor_selected_bbox(ed);
							if (sel) {
								editor_clamp_bbox(
								  ed, sel);
								if (ed->is_video)
									bbox_set_keyframe(
									  &ed->bboxes,
									  sel->id,
									  ed->video.current_frame,
									  sel->x, sel->y,
									  sel->w, sel->h);
							}
							if (prev_state ==
							    DRAG_CREATE)
								editor_txn_abort(ed);
							else
								editor_txn_commit(ed);
							ed->mode = MODE_VIEW;
						}
						ed->render.dirty = 1;
					}
					return;
				}

				ed->active_image = img_idx;
				ed->drag_image_index = img_idx;

				if (ev->key == TB_KEY_MOUSE_LEFT ||
				    ev->key == TB_KEY_MOUSE_RIGHT) {
					if (ed->bboxes.drag_state == DRAG_NONE) {
						int btn;

						btn = (ev->key ==
						       TB_KEY_MOUSE_RIGHT)
							? BBOX_BTN_SECONDARY
							: BBOX_BTN_PRIMARY;
						if (ev->key ==
						    TB_KEY_MOUSE_LEFT)
							editor_clear_selection(ed);
						editor_txn_begin(ed);
						bbox_mouse_down(&ed->bboxes,
								px, py, btn);
						if (ed->bboxes.drag_state ==
						    DRAG_NONE)
							editor_txn_abort(ed);
						else if (ev->key ==
							 TB_KEY_MOUSE_RIGHT)
							editor_select_bbox(
								ed,
								ed->bboxes.selected);
					} else {
						bbox_mouse_move(&ed->bboxes,
								px, py);
						{
							struct bbox *sb =
							  editor_selected_bbox(ed);
							if (sb)
								editor_clamp_bbox(
								  ed, sb);
						}
					}
				ed->mode = MODE_SELECT;
				ed->render.dirty = 1;
			} else if (ev->key == TB_KEY_MOUSE_RELEASE) {
				if (ed->bboxes.drag_state != DRAG_NONE) {
					int id;
					int prev_state =
						ed->bboxes.drag_state;

						id = bbox_mouse_up(&ed->bboxes);
						if (id >= 0) {
							struct bbox *nb =
							  &ed->bboxes.boxes[
							    ed->bboxes.count - 1];
							nb->image_index = img_idx;
							editor_select_bbox(
							  ed,
							  ed->bboxes.count - 1);
							editor_clamp_bbox(ed, nb);
							if (ed->is_video)
								bbox_set_keyframe(
								  &ed->bboxes, id,
								  ed->video.current_frame,
								  nb->x, nb->y,
								  nb->w, nb->h);
							editor_enter_label_edit(
								ed, id, 0, NULL, 1);
						} else {
							struct bbox *sel =
							  editor_selected_bbox(ed);
							if (sel) {
								if (prev_state ==
								    DRAG_MOVE &&
								    img_idx >= 0 &&
								    img_idx <
								    ed->image_count &&
								    img_idx !=
								    sel->image_index) {
									sel->image_index =
									  img_idx;
									sel->x = px -
									  sel->w / 2;
									sel->y = py -
									  sel->h / 2;
								}
								editor_clamp_bbox(
								  ed, sel);
								if (ed->is_video)
									bbox_set_keyframe(
									  &ed->bboxes,
									  sel->id,
									  ed->video.current_frame,
									  sel->x, sel->y,
									  sel->w, sel->h);
							}
							if (prev_state ==
							    DRAG_CREATE)
								editor_txn_abort(ed);
							else
								editor_txn_commit(ed);
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

	/*
	 * termbox reports printable keys in either key or ch depending
	 * on the keymap. Normalize them to key so there is a single
	 * dispatch path below. Label editing still needs the raw ch.
	 */
	if (ed->mode != MODE_LABEL_EDIT &&
	    ev->ch >= 32 && ev->ch < 127) {
		ev->key = ev->ch;
		ev->ch = 0;
	}

	if (ed->show_help) {
		if (ev->key == TB_KEY_ESC || ev->key == '?' ||
		    ev->key == 'q')
			ed->show_help = 0;
		ed->render.dirty = 1;
		return;
	}

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
		if (ev->key == TB_KEY_DELETE) {
			label_handle_delete(ed);
			return;
		}
		if (ev->key == TB_KEY_ARROW_LEFT) {
			label_cursor_left(ed);
			return;
		}
		if (ev->key == TB_KEY_ARROW_RIGHT) {
			label_cursor_right(ed);
			return;
		}
		if (ev->key == TB_KEY_HOME) {
			ed->label_cursor = 0;
			ed->render.dirty = 1;
			return;
		}
		if (ev->key == TB_KEY_END) {
			ed->label_cursor = ed->label_len;
			ed->render.dirty = 1;
			return;
		}
		if (ev->ch != 0)
			label_handle_char(ed, ev->ch);
		return;
	}

	if (ed->is_video) {
		switch (ev->key) {
		case TB_KEY_ARROW_LEFT:
			editor_video_seek(ed,
					  ed->video.current_frame - 1);
			return;
		case TB_KEY_ARROW_RIGHT:
			editor_video_seek(ed,
					  ed->video.current_frame + 1);
			return;
		}
		switch (ev->ch) {
		case '[':
			editor_video_seek(ed,
					  ed->video.current_frame - 30);
			return;
		case ']':
			editor_video_seek(ed,
					  ed->video.current_frame + 30);
			return;
		}
	}

	switch (ev->key) {
	case 'q':
		ed->running = 0;
		return;
	case 'a': {
		int idx, bw, bh, bx, by;
		int new_id;

		if (ed->image_count <= 0)
			return;
		idx = ed->active_image;
		if (idx < 0 || idx >= ed->image_count)
			idx = 0;
		editor_txn_begin(ed);
		bw = ed->images[idx].img.width / 4;
		bh = ed->images[idx].img.height / 4;
		if (bw < 1)
			bw = 1;
		if (bh < 1)
			bh = 1;
		bx = ed->images[idx].img.width / 2 - bw / 2;
		by = ed->images[idx].img.height / 2 - bh / 2;
		new_id = bbox_add(&ed->bboxes, bx, by, bw, bh, idx,
				  NULL, 0);
		if (new_id > 0) {
			editor_select_bbox(ed, ed->bboxes.count - 1);
			if (ed->is_video)
				bbox_set_keyframe(&ed->bboxes, new_id,
						  ed->video.current_frame,
						  bx, by, bw, bh);
			editor_txn_commit(ed);
		} else {
			editor_txn_abort(ed);
		}
		ed->mode = MODE_VIEW;
		ed->render.dirty = 1;
		return;
	}
	case 's':
		editor_save(ed);
		return;
	case 'e':
		editor_edit_selected_label(ed);
		return;
	case 'x':
		ed->arrow_edit_id = 0;
		ed->mode = MODE_ARROW_DRAW;
		ed->arrows.drag_state = ARROW_DRAG_NONE;
		editor_set_status(ed, "click arrow start, then end");
		ed->render.dirty = 1;
		return;
	case 'j':
		editor_edit_arrow_endpoints(ed);
		return;
	case 'c':
		editor_duplicate_selected(ed);
		return;
	case '?':
		ed->show_help = 1;
		ed->render.dirty = 1;
		return;
	case '[':
		if (ed->is_video)
			editor_video_seek(ed,
					  ed->video.current_frame - 30);
		return;
	case ']':
		if (ed->is_video)
			editor_video_seek(ed,
					  ed->video.current_frame + 30);
		return;
	case 'd':
		editor_delete_selected(ed);
		return;
	case 'u':
		editor_txn_abort(ed);
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
		editor_txn_abort(ed);
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
		ed->arrow_edit_id = 0;
		ed->show_help = 0;
		if (ed->mode != MODE_VIEW) {
			ed->mode = MODE_VIEW;
			ed->arrows.drag_state = ARROW_DRAG_NONE;
			ed->render.dirty = 1;
		}
		return;
	case TB_KEY_TAB:
		editor_select_next(ed);
		ed->render.dirty = 1;
		return;
	case TB_KEY_PGUP:
		ed->sidebar_scroll -= 5;
		if (ed->sidebar_scroll < 0)
			ed->sidebar_scroll = 0;
		ed->render.dirty = 1;
		return;
	case TB_KEY_PGDN:
		ed->sidebar_scroll += 5;
		ed->render.dirty = 1;
		return;
	case TB_KEY_ARROW_LEFT:
	case TB_KEY_ARROW_RIGHT:
	case TB_KEY_ARROW_UP:
	case TB_KEY_ARROW_DOWN: {
		int step;
		int resize;
		int dx;
		int dy;

		step = (ev->mod & TB_MOD_SHIFT) ? 10 : 1;
		resize = (ev->mod & TB_MOD_ALT) ? 1 : 0;
		dx = 0;
		dy = 0;
		if (ev->key == TB_KEY_ARROW_LEFT)
			dx = -step;
		else if (ev->key == TB_KEY_ARROW_RIGHT)
			dx = step;
		else if (ev->key == TB_KEY_ARROW_UP)
			dy = -step;
		else
			dy = step;
		editor_nudge_selected(ed, dx, dy, resize);
		return;
	}
	case TB_KEY_ENTER:
		if (ed->mode == MODE_CONFIRM)
			ed->mode = MODE_VIEW;
		ed->render.dirty = 1;
		return;
	}

}

int editor_run(struct editor *ed)
{
	int ret;

	if (ed->forced_proto >= 0)
		ed->proto = (enum term_proto)ed->forced_proto;
	else
		ed->proto = terminal_detect();
	if (ed->proto == TERM_PROTO_UNKNOWN) {
		fprintf(stderr,
			"error: terminal does not support Sixel, Kitty, "
			"or iTerm2 graphics protocol\n"
			"       force one with --proto kitty|sixel|iterm2\n");
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

		editor_video_poll(ed);
		tnow = now_ms();

		if (ed->render.dirty) {
			if (ed->mode == MODE_LABEL_EDIT &&
			    !ed->force_full_render) {
				editor_redraw_status_bar_fast(ed);
				ed->last_render_ms = tnow;
			} else {
				editor_do_render(ed);
				ed->force_full_render = 0;
				ed->last_render_ms = tnow;
			}
		}
	}

	if (ed->proto == TERM_PROTO_KITTY) {
		int k;

		for (k = 0; k < ed->image_count; k++)
			kitty_delete_image(
				ed->render.kitty_image_base + k);
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
