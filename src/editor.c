#include "editor.h"
#include "render.h"
#include "json_util.h"
#include "util.h"

#include "termbox2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void editor_draw_sidebar(struct editor *ed)
{
	int term_w, term_h;
	int sx, y, i, row;
	char buf[256];

	/* Reset sidebar hit-map; each row that doesn't belong
	 * to a bbox stays -1 (no hit). */
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

	/* Vertical separator */
	for (y = 0; y < term_h; y++)
		tb_set_cell(sx, y, '|', TB_WHITE, TB_DEFAULT);

	/* Header */
	row = 0;
	tb_print(sx + 2, row++, TB_YELLOW | TB_BOLD, TB_DEFAULT,
		 "BBoxes (click to select)");
	for (i = 0; i < ed->render.sidebar_w - 3; i++)
		tb_set_cell(sx + 2 + i, row, '-', TB_WHITE,
			    TB_DEFAULT);
	row++;

	if (ed->bboxes.count == 0) {
		tb_print(sx + 2, row, TB_WHITE, TB_DEFAULT,
			 "(none — drag to create)");
		ed->sidebar_row_count = row + 1;
		return;
	}

	/* List each bbox; record id for each row we draw. */
	for (i = 0; i < ed->bboxes.count && row < term_h - 1;
	     i++) {
		struct bbox *b = &ed->bboxes.boxes[i];
		uintattr_t fg = TB_WHITE;
		uintattr_t bg = TB_DEFAULT;
		const char *label;
		int max_rows = (int)(sizeof(ed->sidebar_row_to_id) /
				     sizeof(ed->sidebar_row_to_id[0]));

		/*
		 * NOTE: bbox_manager::selected is an INDEX into
		 * boxes[], not a bbox id (see bbox.c — every
		 * mutation site treats it as an index).
		 * Compare against `i`, not `b->id`.
		 */
		if (i == ed->bboxes.selected) {
			fg = TB_BLACK;
			bg = TB_YELLOW;
		}

		/* Line 1: id + size */
		snprintf(buf, sizeof(buf), " #%d  %dx%d @%d,%d",
			 b->id, b->w, b->h, b->x, b->y);
		if (row < max_rows)
			ed->sidebar_row_to_id[row] = b->id;
		tb_print(sx + 1, row++, fg, bg, buf);
		if (row >= term_h - 1)
			break;

		/* Line 2: label (UTF-8 capable via tb_print).
		 * If this bbox is the live LABEL_EDIT target,
		 * show the in-progress label_buf instead of the
		 * stored label so the sidebar stays in lock-step
		 * with the status bar input field. */
		if (ed->mode == MODE_LABEL_EDIT &&
		    b->id == ed->label_target_id) {
			label = ed->label_buf[0]
				? ed->label_buf
				: "(typing…)";
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
	ed->sidebar_row_count = row;
}

/*
 * Overlay layer: draw all bbox outlines using termbox cells
 * over an already-uploaded base image. This avoids re-encoding
 * and re-transmitting the entire image on every drag/resize
 * frame; only a few dozen cell diffs are emitted per frame.
 *
 * Coordinates: bbox.{x,y,w,h} are in original image pixels.
 * The image is rendered into terminal cells starting at
 * (offset_x, offset_y) with `scale` pixels-per-cell horizontal,
 * and 2 image pixels per cell vertically.
 */
static void editor_draw_bbox_overlay(struct editor *ed)
{
	int i;
	int sidebar_x;
	int img_cols, img_rows;
	struct render_state *rs = &ed->render;

	if (rs->scale <= 0.0f)
		return;

	sidebar_x = (rs->sidebar_w > 0)
		? (tb_width() - rs->sidebar_w)
		: tb_width();
	img_cols = rs->img_display_w;
	img_rows = (rs->img_display_h + 1) / 2;

	for (i = 0; i < ed->bboxes.count; i++) {
		struct bbox *b = &ed->bboxes.boxes[i];
		/* selected is an array index, not an id. */
		int is_sel = (i == ed->bboxes.selected);
		uintattr_t fg = is_sel
			? (TB_YELLOW | TB_BOLD)
			: TB_GREEN;
		int bx, by, bw, bh;
		int x0, y0, x1, y1;
		int cx, cy;

		bx = b->x;
		by = b->y;
		bw = b->w;
		bh = b->h;
		if (bw < 0) {
			bx += bw;
			bw = -bw;
		}
		if (bh < 0) {
			by += bh;
			bh = -bh;
		}

		x0 = rs->offset_x + (int)(bx * rs->scale);
		y0 = rs->offset_y + (int)(by * rs->scale) / 2;
		x1 = rs->offset_x +
		     (int)((bx + bw) * rs->scale) - 1;
		y1 = rs->offset_y +
		     (int)((by + bh) * rs->scale) / 2 - 1;

		if (x0 < rs->offset_x)
			x0 = rs->offset_x;
		if (y0 < rs->offset_y)
			y0 = rs->offset_y;
		if (x1 >= rs->offset_x + img_cols)
			x1 = rs->offset_x + img_cols - 1;
		if (y1 >= rs->offset_y + img_rows)
			y1 = rs->offset_y + img_rows - 1;
		if (x1 >= sidebar_x)
			x1 = sidebar_x - 1;
		if (x1 < x0 || y1 < y0)
			continue;

		/* Top + bottom edges */
		for (cx = x0; cx <= x1; cx++) {
			tb_set_cell(cx, y0, 0x2500, fg, TB_DEFAULT);
			tb_set_cell(cx, y1, 0x2500, fg, TB_DEFAULT);
		}
		/* Left + right edges */
		for (cy = y0; cy <= y1; cy++) {
			tb_set_cell(x0, cy, 0x2502, fg, TB_DEFAULT);
			tb_set_cell(x1, cy, 0x2502, fg, TB_DEFAULT);
		}
		/* Corners */
		tb_set_cell(x0, y0, 0x250C, fg, TB_DEFAULT);
		tb_set_cell(x1, y0, 0x2510, fg, TB_DEFAULT);
		tb_set_cell(x0, y1, 0x2514, fg, TB_DEFAULT);
		tb_set_cell(x1, y1, 0x2518, fg, TB_DEFAULT);

		/* Selected: corner handles in inverted style */
		if (is_sel) {
			tb_set_cell(x0, y0, '+',
				    TB_BLACK, TB_YELLOW);
			tb_set_cell(x1, y0, '+',
				    TB_BLACK, TB_YELLOW);
			tb_set_cell(x0, y1, '+',
				    TB_BLACK, TB_YELLOW);
			tb_set_cell(x1, y1, '+',
				    TB_BLACK, TB_YELLOW);
		}

		/* Label near top-left */
		if (b->label[0]) {
			int lx = x0 + 1;
			int ly = (y0 > rs->offset_y) ? y0 - 1 : y0;

			if (lx < sidebar_x)
				tb_print(lx, ly, fg, TB_DEFAULT,
					 b->label);
		}
	}

	/* In-progress drag: show the rubber-band rectangle. */
	if (ed->bboxes.drag_state == DRAG_CREATE) {
		int sx = ed->bboxes.start_x;
		int sy = ed->bboxes.start_y;
		int ex = ed->bboxes.cur_x;
		int ey = ed->bboxes.cur_y;
		int x0, y0, x1, y1, cx, cy;

		if (sx > ex) {
			int t = sx; sx = ex; ex = t;
		}
		if (sy > ey) {
			int t = sy; sy = ey; ey = t;
		}
		x0 = rs->offset_x + (int)(sx * rs->scale);
		y0 = rs->offset_y + (int)(sy * rs->scale) / 2;
		x1 = rs->offset_x + (int)(ex * rs->scale) - 1;
		y1 = rs->offset_y + (int)(ey * rs->scale) / 2 - 1;
		if (x0 < rs->offset_x)
			x0 = rs->offset_x;
		if (y0 < rs->offset_y)
			y0 = rs->offset_y;
		if (x1 >= rs->offset_x + img_cols)
			x1 = rs->offset_x + img_cols - 1;
		if (y1 >= rs->offset_y + img_rows)
			y1 = rs->offset_y + img_rows - 1;
		if (x1 >= sidebar_x)
			x1 = sidebar_x - 1;
		if (x1 >= x0 && y1 >= y0) {
			for (cx = x0; cx <= x1; cx++) {
				tb_set_cell(cx, y0, 0x2550,
					    TB_CYAN | TB_BOLD,
					    TB_DEFAULT);
				tb_set_cell(cx, y1, 0x2550,
					    TB_CYAN | TB_BOLD,
					    TB_DEFAULT);
			}
			for (cy = y0; cy <= y1; cy++) {
				tb_set_cell(x0, cy, 0x2551,
					    TB_CYAN | TB_BOLD,
					    TB_DEFAULT);
				tb_set_cell(x1, cy, 0x2551,
					    TB_CYAN | TB_BOLD,
					    TB_DEFAULT);
			}
		}
	}
}

static void editor_redraw_status_bar_fast(struct editor *ed)
{
	int term_w, term_h;
	int img_rows;
	int x, y, i;
	char line1[256], line2[256];
	struct bbox *sel;
	const char *mode_str;
	int len1, len2;

	term_w = tb_width();
	term_h = tb_height();
	if (term_w <= 0 || term_h <= 0)
		return;

	tb_clear();

	img_rows = (ed->render.img_display_h + 1) / 2;
	if (img_rows < 0)
		img_rows = 0;
	y = img_rows;

	for (x = 0; x < term_w; x++)
		tb_set_cell(x, y, '-', TB_WHITE, TB_DEFAULT);

	switch (ed->mode) {
	case MODE_VIEW: mode_str = "VIEW"; break;
	case MODE_SELECT: mode_str = "SELECT"; break;
	case MODE_CONFIRM: mode_str = "CONFIRM"; break;
	case MODE_LABEL_EDIT: mode_str = "LABEL"; break;
	default: mode_str = "VIEW"; break;
	}
	sel = bbox_get_selected(&ed->bboxes);
	if (sel) {
		len1 = snprintf(line1, sizeof(line1),
			" %s [%dx%d] | BBox: %d (sel #%d %dx%d@%d,%d) | %s | Undo: %d",
			ed->img.path[0] ? ed->img.path : "(none)",
			ed->img.width, ed->img.height,
			ed->bboxes.count, sel->id,
			sel->w, sel->h, sel->x, sel->y,
			mode_str, ed->undo_count);
	} else {
		len1 = snprintf(line1, sizeof(line1),
			" %s [%dx%d] | BBox: %d (none sel) | %s | Undo: %d",
			ed->img.path[0] ? ed->img.path : "(none)",
			ed->img.width, ed->img.height,
			ed->bboxes.count, mode_str, ed->undo_count);
	}
	for (i = 0; i < len1 && i < term_w; i++)
		tb_set_cell(i, y + 1, (uint32_t)line1[i],
			    TB_WHITE, TB_DEFAULT);

	if (ed->mode == MODE_LABEL_EDIT) {
		len2 = snprintf(line2, sizeof(line2),
			" Label: %s_  (Enter=confirm, ESC=cancel, Backspace=delete)",
			ed->label_buf);
	} else {
		len2 = snprintf(line2, sizeof(line2),
			" L=draw  R=select/move  [a]dd [d]el [e]label [s]ave [Tab]next [u]ndo [r]edo [q]uit");
	}
	(void)len2;
	/*
	 * Use tb_print so multibyte UTF-8 (e.g. CJK characters
	 * typed into the label) is decoded and rendered as wide
	 * cells correctly, instead of one byte per cell.
	 */
	tb_print(0, y + 2, TB_WHITE, TB_DEFAULT, line2);

	editor_draw_sidebar(ed);

	/*
	 * tb_clear() above wipes the back buffer to spaces,
	 * which would erase every bbox cell drawn last frame.
	 * Re-paint the bbox overlay so existing boxes (and any
	 * label being edited) stay visible while the user is
	 * typing in MODE_LABEL_EDIT.
	 */
	editor_draw_bbox_overlay(ed);

	/*
	 * Plain tb_present here: only a few dozen cell diffs
	 * (status bar + sidebar label + maybe the live label
	 * inside the sidebar) get sent. Avoid wrapping with
	 * the synchronized-output sequence (BSU/ESU) — for a
	 * tiny diff like this it adds an extra round trip and
	 * can make typing feel laggy without any tearing
	 * benefit. The image was uploaded once already and is
	 * not part of this frame's output.
	 */
	tb_present();

	ed->render.dirty = 0;
}

static void editor_do_render(struct editor *ed)
{
	unsigned char *display_pixels;
	int display_w, display_h;
	int term_w, term_h;
	int img_rows;
	int x, y, i;
	char *img_data;

	term_w = tb_width();
	term_h = tb_height();

	if (term_w <= 0 || term_h <= 0)
		return;

	if (!ed->img.pixels || ed->img.width <= 0 || ed->img.height <= 0)
		return;

	render_calc_fit(&ed->render, ed->img.width, ed->img.height,
			term_w, term_h);

	display_w = ed->render.img_display_w;
	display_h = ed->render.img_display_h;

	if (display_w <= 0 || display_h <= 0)
		return;

	/*
	 * Render flow:
	 * 1. tb_clear() - reset back buffer to spaces
	 * 2. Set status bar cells in back buffer via tb_set_cell
	 * 3. tb_present() - sync front buffer, write cell diffs,
	 *    flush output buffer (clears screen, writes status bar)
	 * 4. Send image data via tb_send (raw escapes)
	 * 5. tb_present() - no cell diffs (front==back already),
	 *    flushes image data to terminal
	 *
	 * Step 3 must happen before step 4 so that tb_present's
	 * cell diff output doesn't overwrite the image.
	 */

	tb_clear();

	img_rows = (display_h + 1) / 2;

	/* Status bar separator */
	y = img_rows;
	for (x = 0; x < term_w; x++)
		tb_set_cell(x, y, '-', TB_WHITE, TB_DEFAULT);

	/* Status line 1 */
	{
		char line1[256];
		const char *mode_str;
		int len;
		struct bbox *sel;

		switch (ed->mode) {
		case MODE_VIEW: mode_str = "VIEW"; break;
		case MODE_SELECT: mode_str = "SELECT"; break;
		case MODE_CONFIRM: mode_str = "CONFIRM"; break;
		case MODE_LABEL_EDIT: mode_str = "LABEL"; break;
		default: mode_str = "VIEW"; break;
		}

		sel = bbox_get_selected(&ed->bboxes);
		if (sel) {
			len = snprintf(
				line1, sizeof(line1),
				" %s [%dx%d] | BBox: %d (sel #%d %dx%d@%d,%d) | %s | Undo: %d",
				ed->img.path[0] ? ed->img.path : "(none)",
				ed->img.width, ed->img.height,
				ed->bboxes.count, sel->id,
				sel->w, sel->h, sel->x, sel->y,
				mode_str, ed->undo_count);
		} else {
			len = snprintf(
				line1, sizeof(line1),
				" %s [%dx%d] | BBox: %d (none sel) | %s | Undo: %d",
				ed->img.path[0] ? ed->img.path : "(none)",
				ed->img.width, ed->img.height,
				ed->bboxes.count, mode_str,
				ed->undo_count);
		}
		for (i = 0; i < len && i < term_w; i++)
			tb_set_cell(i, y + 1, (uint32_t)line1[i],
				    TB_WHITE, TB_DEFAULT);
	}

	/* Status line 2 */
	{
		char line2[256];
		int len;

		if (ed->mode == MODE_LABEL_EDIT) {
			len = snprintf(line2, sizeof(line2),
				       " Label: %s_  (Enter=confirm, ESC=cancel, Backspace=delete)",
				       ed->label_buf);
		} else {
			len = snprintf(line2, sizeof(line2),
				       " L=draw  R=select/move  [a]dd [d]el [e]label [s]ave [Tab]next [u]ndo [r]edo [q]uit");
		}
		(void)len;
		/* tb_print handles UTF-8 + wide chars correctly. */
		tb_print(0, y + 2, TB_WHITE, TB_DEFAULT, line2);
	}

	editor_draw_sidebar(ed);

	/*
	 * Layered render decision:
	 *
	 *   need_image_upload == true  -> full path: clear,
	 *     write status cells, present, send image, present
	 *     again, draw bbox cell overlay, present once more.
	 *
	 *   need_image_upload == false -> overlay-only path:
	 *     skip image encoding/transmission entirely. The
	 *     base image is already sitting on the terminal's
	 *     graphics layer (Kitty placement / iTerm2 inline /
	 *     Sixel pixels), and bboxes/status are pure
	 *     termbox cells, so a single tb_present sends only
	 *     the cell diffs needed to update the rubber-band
	 *     and selected outline. This is what makes drag
	 *     feedback fast.
	 */
	{
		int need_image_upload =
			(!ed->image_uploaded) ||
			(ed->image_gen != ed->last_uploaded_gen) ||
			(term_w != ed->last_term_w) ||
			(term_h != ed->last_term_h);

		if (need_image_upload) {
			/*
			 * Full upload path: status bar first, then
			 * image data, then bbox overlay. Wrap the
			 * heavy pixel transmission in DEC 2026
			 * synchronized output so the terminal only
			 * composites once at the end. For the pure
			 * overlay path below we skip BSU/ESU — those
			 * frames are tiny cell diffs and the extra
			 * round-trip just adds latency (visible when
			 * clicking sidebar rows).
			 */
			tb_send("\033[?2026h", 8);
			tb_present();

			tb_send("\033[H", 3);

			img_data = NULL;

			if (ed->proto == TERM_PROTO_KITTY) {
				int disp_cols, disp_rows;

				disp_cols = display_w;
				disp_rows = (display_h + 1) / 2;
				if (disp_cols < 1)
					disp_cols = 1;
				if (disp_rows < 1)
					disp_rows = 1;

				img_data = render_image_kitty(
					&ed->arenas.arenas[
						ARENA_COMMAND],
					ed->img.pixels,
					ed->img.width,
					ed->img.height,
					ed->img.channels,
					ed->render
						.kitty_placement_id,
					disp_cols, disp_rows);
				if (img_data)
					tb_send(img_data,
						strlen(img_data));
			} else if (ed->proto == TERM_PROTO_ITERM2) {
				int disp_cols, disp_rows;

				disp_cols = display_w;
				disp_rows = (display_h + 1) / 2;
				if (disp_cols < 1)
					disp_cols = 1;
				if (disp_rows < 1)
					disp_rows = 1;

				img_data = render_image_iterm2(
					&ed->arenas.arenas[
						ARENA_COMMAND],
					ed->img.pixels,
					ed->img.width,
					ed->img.height,
					ed->img.channels,
					disp_cols, disp_rows);
				if (img_data)
					tb_send(img_data,
						strlen(img_data));
			} else {
				display_pixels = image_resize(
					&ed->arenas.arenas[
						ARENA_COMMAND],
					ed->img.pixels,
					ed->img.width,
					ed->img.height,
					ed->img.channels,
					display_w, display_h);
				if (display_pixels) {
					img_data =
						render_image_sixel(
						&ed->arenas.arenas[
						ARENA_COMMAND],
						display_pixels,
						display_w,
						display_h,
						ed->img.channels);
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

		/*
		 * Overlay layer: draw bbox outlines as termbox
		 * cells. tb_present below will only emit cell
		 * diffs (the few cells that changed since the
		 * last frame), leaving the graphics layer alone.
		 * No BSU/ESU here — see comment above.
		 */
		editor_draw_bbox_overlay(ed);
		tb_present();
	}

	(void)x;
	(void)i;
	ed->render.dirty = 0;
	arena_reset(&ed->arenas.arenas[ARENA_COMMAND]);
}

int editor_init(struct editor *ed)
{
	memset(ed, 0, sizeof(*ed));
	arena_set_init(&ed->arenas);
	bbox_manager_init(&ed->bboxes);
	history_init(&ed->history, 64);
	render_init(&ed->render, TERM_PROTO_UNKNOWN);
	ed->mode = MODE_VIEW;
	ed->running = 0;
	ed->undo_count = 0;
	ed->label_target_id = -1;
	return 0;
}

void editor_free(struct editor *ed)
{
	if (ed->running)
		editor_stop(ed);
	history_free(&ed->history);
	arena_set_free(&ed->arenas);
}

int editor_open_image(struct editor *ed, const char *path)
{
	int ret;

	arena_reset(&ed->arenas.arenas[ARENA_IMAGE]);
	ret = image_load(&ed->arenas.arenas[ARENA_IMAGE], path, &ed->img);
	if (ret < 0)
		return ret;
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
				&ed->img);
	if (ret < 0)
		return ret;
	ed->image_gen++;
	ed->image_uploaded = 0;
	ed->render.dirty = 1;
	return 0;
}

static cJSON *snapshot_bboxes(struct editor *ed)
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
	return snap;
}

static void restore_bboxes(struct editor *ed, cJSON *snap)
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
}

static void editor_push_history(struct editor *ed, enum op_type type)
{
	cJSON *snap;

	snap = snapshot_bboxes(ed);
	if (snap)
		history_push(&ed->history, type, snap);
}

static void editor_save(struct editor *ed)
{
	cJSON *root;
	cJSON *bboxes_arr;
	char *result;
	int i;

	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "path", ed->img.path);
	cJSON_AddNumberToObject(root, "width", ed->img.width);
	cJSON_AddNumberToObject(root, "height", ed->img.height);

	bboxes_arr = cJSON_CreateArray();
	for (i = 0; i < ed->bboxes.count; i++)
		cJSON_AddItemToArray(bboxes_arr,
				     bbox_to_json(&ed->bboxes.boxes[i]));
	cJSON_AddItemToObject(root, "bboxes", bboxes_arr);

	result = cJSON_PrintUnformatted(root);
	fprintf(stderr, "%s\n", result);
	free(result);
	cJSON_Delete(root);
}

void editor_handle_event(struct editor *ed, struct tb_event *ev)
{
	if (ev->type == TB_EVENT_RESIZE) {
		ed->render.dirty = 1;
		return;
	}

	if (ev->type == TB_EVENT_MOUSE) {
		int px, py;
		int img_cols, img_rows;
		int in_image;
		int sidebar_x;

		/*
		 * Sidebar click: hit-test against the per-row map
		 * built by editor_draw_sidebar(). A press inside
		 * the sidebar selects that bbox. Releases inside
		 * the sidebar are ignored (no drag semantics there).
		 */
		sidebar_x = (ed->render.sidebar_w > 0)
			? (tb_width() - ed->render.sidebar_w)
			: -1;
		if (sidebar_x >= 0 && ev->x > sidebar_x) {
			if (ev->key == TB_KEY_MOUSE_LEFT &&
			    ed->bboxes.drag_state == DRAG_NONE &&
			    ev->y >= 0 &&
			    ev->y < (int)(sizeof(ed->sidebar_row_to_id) /
					  sizeof(ed->sidebar_row_to_id[0]))) {
				int hit_id = ed->sidebar_row_to_id[ev->y];

				/*
				 * Sidebar click is a *pure selection*
				 * gesture: just update `selected` and
				 * stay in VIEW. Do not push history,
				 * do not auto-enter LABEL_EDIT — both
				 * add visible latency to what should
				 * be a near-instant select. Press 'e'
				 * afterwards to edit the label.
				 *
				 * NOTE: bbox_manager::selected is an
				 * INDEX into boxes[], not a bbox id.
				 * sidebar_row_to_id stores ids, so we
				 * must translate id -> index here.
				 * Without this, bbox_get_selected()
				 * returns the wrong bbox (or NULL),
				 * which made shortcuts like 'e' / 'd'
				 * silently fail after a sidebar click.
				 */
				if (hit_id >= 0) {
					int k;

					for (k = 0; k < ed->bboxes.count;
					     k++) {
						if (ed->bboxes.boxes[k].id ==
						    hit_id) {
							if (ed->bboxes
								.selected !=
							    k) {
								ed->bboxes
								    .selected =
									k;
								ed->mode =
								    MODE_VIEW;
								ed->render
								    .dirty = 1;
							}
							break;
						}
					}
				}
			}
			return;
		}

		/*
		 * Image is rendered into the rectangle:
		 *   cols [offset_x, offset_x + img_display_w)
		 *   rows [offset_y, offset_y + (img_display_h+1)/2)
		 * Treat clicks outside this region as out-of-bounds:
		 * either ignore them, or terminate any active drag.
		 */
		img_cols = ed->render.img_display_w;
		img_rows = (ed->render.img_display_h + 1) / 2;
		in_image = (ev->x >= ed->render.offset_x &&
			    ev->x < ed->render.offset_x + img_cols &&
			    ev->y >= ed->render.offset_y &&
			    ev->y < ed->render.offset_y + img_rows);

		if (!in_image) {
			/* Out-of-image click: drop press, but let
			 * release end any pending drag cleanly. */
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

		term_to_pixel(ev->x, ev->y, &px, &py,
			      ed->img.width, ed->img.height,
			      ed->render.offset_x,
			      ed->render.offset_y,
			      ed->render.scale);

		if (ev->key == TB_KEY_MOUSE_LEFT ||
		    ev->key == TB_KEY_MOUSE_RIGHT) {
			if (ed->bboxes.drag_state == DRAG_NONE) {
				int btn = (ev->key ==
					   TB_KEY_MOUSE_RIGHT)
					? BBOX_BTN_SECONDARY
					: BBOX_BTN_PRIMARY;

				editor_push_history(ed, OP_ADD_BBOX);
				bbox_mouse_down(&ed->bboxes, px, py,
						btn);
			} else {
				bbox_mouse_move(&ed->bboxes, px, py);
			}
			ed->mode = MODE_SELECT;
			ed->render.dirty = 1;
		} else if (ev->key == TB_KEY_MOUSE_RELEASE) {
			if (ed->bboxes.drag_state != DRAG_NONE) {
				int id;

				id = bbox_mouse_up(&ed->bboxes);
				if (id >= 0) {
					/*
					 * Just created a new bbox: jump
					 * straight into label-edit mode
					 * so the user can type a label.
					 *
					 * NOTE: bbox_add() (called from
					 * bbox_mouse_up) already sets
					 * selected = count - 1, which is
					 * the correct *index*. Do NOT
					 * overwrite it with `id` here —
					 * `selected` is an index, not an
					 * id, so writing the id corrupts
					 * subsequent bbox_get_selected().
					 */
					editor_push_history(ed,
						OP_EDIT_LABEL);
					ed->mode = MODE_LABEL_EDIT;
					ed->label_target_id = id;
					ed->label_buf[0] = '\0';
					ed->label_len = 0;
				} else {
					ed->mode = MODE_VIEW;
				}
				ed->render.dirty = 1;
			}
		} else if (ev->key == TB_KEY_MOUSE_RIGHT) {
			ed->bboxes.drag_state = DRAG_NONE;
			ed->mode = MODE_VIEW;
			ed->render.dirty = 1;
		}
		return;
	}

	if (ev->type != TB_EVENT_KEY)
		return;

	/*
	 * LABEL_EDIT mode: capture keystrokes into the bbox label
	 * buffer instead of running normal editor shortcuts.
	 */
	if (ed->mode == MODE_LABEL_EDIT) {
		struct bbox *target = NULL;
		int i;

		for (i = 0; i < ed->bboxes.count; i++) {
			if (ed->bboxes.boxes[i].id == ed->label_target_id) {
				target = &ed->bboxes.boxes[i];
				break;
			}
		}

		if (ev->key == TB_KEY_ENTER) {
			/* Commit label */
			if (target) {
				strncpy(target->label, ed->label_buf,
					BBOX_LABEL_MAX - 1);
				target->label[BBOX_LABEL_MAX - 1] = '\0';
			}
			ed->mode = MODE_VIEW;
			ed->label_buf[0] = '\0';
			ed->label_len = 0;
			ed->label_target_id = -1;
			ed->render.dirty = 1;
			return;
		}
		if (ev->key == TB_KEY_ESC) {
			/* Cancel: history was already pushed; revert via undo
			 * is the user's responsibility. Just discard buffer. */
			ed->mode = MODE_VIEW;
			ed->label_buf[0] = '\0';
			ed->label_len = 0;
			ed->label_target_id = -1;
			ed->render.dirty = 1;
			return;
		}
		if (ev->key == TB_KEY_BACKSPACE ||
		    ev->key == TB_KEY_BACKSPACE2) {
			if (ed->label_len > 0) {
				/*
				 * Delete one UTF-8 codepoint, not just
				 * one byte. Forward-scan label_buf to
				 * find the last codepoint boundary:
				 * a UTF-8 leading byte is anything that
				 * is NOT a continuation byte (10xxxxxx).
				 * Truncate at the last leading byte.
				 *
				 * This is robust against any byte
				 * sequence in the buffer, including
				 * malformed input — we always trim down
				 * to the previous codepoint start.
				 */
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
			return;
		}
		if (ev->ch != 0) {
			/*
			 * Encode the codepoint as UTF-8 so CJK and
			 * other non-ASCII input works. termbox2
			 * decodes incoming UTF-8 input into a single
			 * codepoint in ev->ch.
			 */
			unsigned int cp = ev->ch;
			char enc[4];
			int n = 0;

			if (cp < 0x20) {
				/* Skip control codes */
				return;
			}
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
			if (ed->label_len + n < BBOX_LABEL_MAX - 1) {
				int k;

				for (k = 0; k < n; k++)
					ed->label_buf[ed->label_len++] =
						enc[k];
				ed->label_buf[ed->label_len] = '\0';
				ed->render.dirty = 1;
			}
		}
		return;
	}

	/* Check ev->key first — termbox2 sets it for both ASCII and special */
	switch (ev->key) {
	case 'q':
		ed->running = 0;
		return;
	case 'a': {
		/* Add bbox at image center, 1/4 size */
		int bw, bh, bx, by;

		editor_push_history(ed, OP_ADD_BBOX);
		bw = ed->img.width / 4;
		bh = ed->img.height / 4;
		bx = ed->img.width / 2 - bw / 2;
		by = ed->img.height / 2 - bh / 2;
		bbox_add(&ed->bboxes, bx, by, bw, bh, NULL, 0);
		ed->mode = MODE_SELECT;
		ed->render.dirty = 1;
		return;
	}
	case 's':
		editor_save(ed);
		return;
	case 'e': {
		/* Enter label-edit mode for selected bbox */
		struct bbox *sel = bbox_get_selected(&ed->bboxes);

		if (sel) {
			editor_push_history(ed, OP_EDIT_LABEL);
			ed->mode = MODE_LABEL_EDIT;
			ed->label_target_id = sel->id;
			strncpy(ed->label_buf, sel->label,
				BBOX_LABEL_MAX - 1);
			ed->label_buf[BBOX_LABEL_MAX - 1] = '\0';
			ed->label_len = (int)strlen(ed->label_buf);
			ed->render.dirty = 1;
		}
		return;
	}
	case 'd': {
		struct bbox *sel;

		sel = bbox_get_selected(&ed->bboxes);
		if (sel) {
			editor_push_history(ed, OP_REMOVE_BBOX);
			bbox_remove(&ed->bboxes, sel->id);
			ed->render.dirty = 1;
		}
		return;
	}
	case 'u':
		if (history_can_undo(&ed->history)) {
			cJSON *cur;
			cJSON *prev;

			cur = snapshot_bboxes(ed);
			prev = history_undo(&ed->history, cur);
			if (prev) {
				restore_bboxes(ed, prev);
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
			cJSON *cur;
			cJSON *next;

			cur = snapshot_bboxes(ed);
			next = history_redo(&ed->history, cur);
			if (next) {
				restore_bboxes(ed, next);
				cJSON_Delete(next);
				if (ed->undo_count > 0)
					ed->undo_count--;
				ed->render.dirty = 1;
			} else {
				cJSON_Delete(cur);
			}
		}
		return;
	case 'h': {
		struct bbox *sel;

		sel = bbox_get_selected(&ed->bboxes);
		if (sel && sel->w > 4) {
			editor_push_history(ed, OP_RESIZE_BBOX);
			sel->w -= 4;
			sel->x += 2;
			ed->render.dirty = 1;
		}
		return;
	}
	case 'j': {
		struct bbox *sel;

		sel = bbox_get_selected(&ed->bboxes);
		if (sel && sel->h > 4) {
			editor_push_history(ed, OP_RESIZE_BBOX);
			sel->h += 4;
			ed->render.dirty = 1;
		}
		return;
	}
	case 'k': {
		struct bbox *sel;

		sel = bbox_get_selected(&ed->bboxes);
		if (sel && sel->h > 4) {
			editor_push_history(ed, OP_RESIZE_BBOX);
			sel->h -= 4;
			sel->y += 2;
			ed->render.dirty = 1;
		}
		return;
	}
	case 'l': {
		struct bbox *sel;

		sel = bbox_get_selected(&ed->bboxes);
		if (sel) {
			editor_push_history(ed, OP_RESIZE_BBOX);
			sel->w += 4;
			ed->render.dirty = 1;
		}
		return;
	}
	case 'H': {
		struct bbox *sel;

		sel = bbox_get_selected(&ed->bboxes);
		if (sel) {
			editor_push_history(ed, OP_MOVE_BBOX);
			sel->x -= 4;
			ed->render.dirty = 1;
		}
		return;
	}
	case 'J': {
		struct bbox *sel;

		sel = bbox_get_selected(&ed->bboxes);
		if (sel) {
			editor_push_history(ed, OP_MOVE_BBOX);
			sel->y += 4;
			ed->render.dirty = 1;
		}
		return;
	}
	case 'K': {
		struct bbox *sel;

		sel = bbox_get_selected(&ed->bboxes);
		if (sel) {
			editor_push_history(ed, OP_MOVE_BBOX);
			sel->y -= 4;
			ed->render.dirty = 1;
		}
		return;
	}
	case 'L': {
		struct bbox *sel;

		sel = bbox_get_selected(&ed->bboxes);
		if (sel) {
			editor_push_history(ed, OP_MOVE_BBOX);
			sel->x += 4;
			ed->render.dirty = 1;
		}
		return;
	}
	case TB_KEY_CTRL_C:
	case TB_KEY_CTRL_Q:
		ed->running = 0;
		return;
	case TB_KEY_ESC:
		/*
		 * Don't exit on bare ESC: terminal graphics protocols
		 * (Kitty/iTerm2) and many escape sequences begin with
		 * ESC and may be partially mis-parsed as a lone ESC,
		 * which would otherwise quit the editor unexpectedly
		 * (e.g. right after rendering a Kitty image, the
		 * terminal echoes back \x1b_Gi=N;OK\x1b\\ ).
		 * Use 'q' or Ctrl-C/Ctrl-Q to quit instead.
		 */
		if (ed->mode != MODE_VIEW) {
			ed->mode = MODE_VIEW;
			ed->render.dirty = 1;
		}
		return;
	case TB_KEY_TAB:
		bbox_select_next(&ed->bboxes);
		ed->render.dirty = 1;
		return;
	case TB_KEY_ENTER:
		if (ed->mode == MODE_CONFIRM)
			ed->mode = MODE_VIEW;
		ed->render.dirty = 1;
		return;
	}

	/* Fallback: some terminals only set ev->ch, not ev->key */
	if (ev->ch) {
		switch (ev->ch) {
		case 'q':
			ed->running = 0;
			break;
		case 'a': {
			int bw, bh, bx, by;

			editor_push_history(ed, OP_ADD_BBOX);
			bw = ed->img.width / 4;
			bh = ed->img.height / 4;
			bx = ed->img.width / 2 - bw / 2;
			by = ed->img.height / 2 - bh / 2;
			bbox_add(&ed->bboxes, bx, by, bw, bh, NULL, 0);
			ed->mode = MODE_SELECT;
			ed->render.dirty = 1;
			break;
		}
		case 's':
			editor_save(ed);
			break;
		case 'd': {
			struct bbox *sel;

			sel = bbox_get_selected(&ed->bboxes);
			if (sel) {
				editor_push_history(ed, OP_REMOVE_BBOX);
				bbox_remove(&ed->bboxes, sel->id);
				ed->render.dirty = 1;
			}
			break;
		}
		case 'u':
			if (history_can_undo(&ed->history)) {
				cJSON *cur;
				cJSON *prev;

				cur = snapshot_bboxes(ed);
				prev = history_undo(&ed->history, cur);
				if (prev) {
					restore_bboxes(ed, prev);
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
				cJSON *cur;
				cJSON *next;

				cur = snapshot_bboxes(ed);
				next = history_redo(&ed->history, cur);
				if (next) {
					restore_bboxes(ed, next);
					cJSON_Delete(next);
					if (ed->undo_count > 0)
						ed->undo_count--;
					ed->render.dirty = 1;
				} else {
					cJSON_Delete(cur);
				}
			}
			break;
		case 'h': {
			struct bbox *sel;

			sel = bbox_get_selected(&ed->bboxes);
			if (sel && sel->w > 4) {
				editor_push_history(ed, OP_RESIZE_BBOX);
				sel->w -= 4;
				sel->x += 2;
				ed->render.dirty = 1;
			}
			break;
		}
		case 'j': {
			struct bbox *sel;

			sel = bbox_get_selected(&ed->bboxes);
			if (sel && sel->h > 4) {
				editor_push_history(ed, OP_RESIZE_BBOX);
				sel->h += 4;
				ed->render.dirty = 1;
			}
			break;
		}
		case 'k': {
			struct bbox *sel;

			sel = bbox_get_selected(&ed->bboxes);
			if (sel && sel->h > 4) {
				editor_push_history(ed, OP_RESIZE_BBOX);
				sel->h -= 4;
				sel->y += 2;
				ed->render.dirty = 1;
			}
			break;
		}
		case 'l': {
			struct bbox *sel;

			sel = bbox_get_selected(&ed->bboxes);
			if (sel) {
				editor_push_history(ed, OP_RESIZE_BBOX);
				sel->w += 4;
				ed->render.dirty = 1;
			}
			break;
		}
		case 'H': {
			struct bbox *sel;

			sel = bbox_get_selected(&ed->bboxes);
			if (sel) {
				editor_push_history(ed, OP_MOVE_BBOX);
				sel->x -= 4;
				ed->render.dirty = 1;
			}
			break;
		}
		case 'J': {
			struct bbox *sel;

			sel = bbox_get_selected(&ed->bboxes);
			if (sel) {
				editor_push_history(ed, OP_MOVE_BBOX);
				sel->y += 4;
				ed->render.dirty = 1;
			}
			break;
		}
		case 'K': {
			struct bbox *sel;

			sel = bbox_get_selected(&ed->bboxes);
			if (sel) {
				editor_push_history(ed, OP_MOVE_BBOX);
				sel->y -= 4;
				ed->render.dirty = 1;
			}
			break;
		}
		case 'L': {
			struct bbox *sel;

			sel = bbox_get_selected(&ed->bboxes);
			if (sel) {
				editor_push_history(ed, OP_MOVE_BBOX);
				sel->x += 4;
				ed->render.dirty = 1;
			}
			break;
		}
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

	/*
	 * Sync front/back buffers after init so that
	 * subsequent tb_present() calls won't generate
	 * cell diffs for the entire screen.
	 */
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
		long long now;
		int wait_ms;

		/*
		 * In LABEL_EDIT we want zero-latency typing: block
		 * indefinitely until a key arrives, then drain the
		 * rest of the burst non-blocking and render once.
		 * In other modes we keep the 30ms tick for animation
		 * and external state changes.
		 */
		wait_ms = (ed->mode == MODE_LABEL_EDIT) ? -1 : 30;

		/* Drain all pending events before rendering */
		for (nev = 0; nev < 64; nev++) {
			ret = tb_peek_event(&ev,
				nev == 0 ? wait_ms : 0);
			if (ret != TB_OK)
				break;
			editor_handle_event(ed, &ev);
			if (!ed->running)
				break;
		}

		now = now_ms();

		if (ed->render.dirty) {
			if (ed->mode == MODE_LABEL_EDIT) {
				/*
				 * In label-edit mode the image hasn't
				 * changed, so avoid the expensive
				 * PNG-encode + base64 + retransmit
				 * path; only refresh the status bar.
				 * This keeps typing latency near zero.
				 */
				editor_redraw_status_bar_fast(ed);
				ed->last_render_ms = now;
			} else {
				/*
				 * Layered render: editor_do_render()
				 * skips image upload when only the
				 * bbox overlay changed, so dragging
				 * is fast without throttling.
				 */
				editor_do_render(ed);
				ed->last_render_ms = now;
			}
		}
	}

	tb_shutdown();
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
