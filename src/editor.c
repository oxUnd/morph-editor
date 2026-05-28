#include "editor.h"
#include "render.h"
#include "json_util.h"
#include "util.h"

#include "termbox2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

		len = snprintf(line2, sizeof(line2),
			       " [a]dd [d]el [s]ave [Tab]next [u]ndo [r]edo [q]uit | h/j/k/l resize  H/J/K/L move");
		for (i = 0; i < len && i < term_w; i++)
			tb_set_cell(i, y + 2, (uint32_t)line2[i],
				    TB_WHITE, TB_DEFAULT);
	}

	/*
	 * First present: sync front buffer with back buffer.
	 * This clears the screen and writes the status bar cells.
	 * After this call, front == back, so subsequent presents
	 * won't generate cell diffs that could overwrite the image.
	 */
	tb_present();

	/* Now send image data as raw escape sequences */
	tb_send("\033[H", 3);

	img_data = NULL;

	if (ed->proto == TERM_PROTO_KITTY) {
		int disp_cols, disp_rows;

		if (ed->render.kitty_placement_id > 1)
			kitty_delete_placement(
				ed->render.kitty_placement_id - 1);

		/*
		 * For Kitty: send original-size pixels and let the
		 * terminal scale to the requested cell box. This
		 * avoids double down-scaling and yields a much sharper,
		 * larger image (display_w/display_h were sized in
		 * pixels for half-block proto and would otherwise be
		 * ~1px per terminal cell).
		 */
		disp_cols = display_w;
		disp_rows = (display_h + 1) / 2;
		if (disp_cols < 1)
			disp_cols = 1;
		if (disp_rows < 1)
			disp_rows = 1;

		display_pixels = arena_alloc(
			&ed->arenas.arenas[ARENA_COMMAND],
			(size_t)ed->img.width * ed->img.height *
			ed->img.channels);
		if (display_pixels) {
			memcpy(display_pixels, ed->img.pixels,
			       (size_t)ed->img.width * ed->img.height *
			       ed->img.channels);
			render_bboxes_on_pixels_raw(
				display_pixels, ed->img.width,
				ed->img.height, ed->img.channels,
				&ed->bboxes);
			img_data = render_image_kitty(
				&ed->arenas.arenas[ARENA_COMMAND],
				display_pixels, ed->img.width,
				ed->img.height, ed->img.channels,
				ed->render.kitty_placement_id,
				disp_cols, disp_rows);
			if (img_data) {
				tb_send(img_data, strlen(img_data));
				ed->render.kitty_placement_id++;
			}
		}
	} else if (ed->proto == TERM_PROTO_ITERM2) {
		int disp_cols, disp_rows;

		/*
		 * iTerm2: send original-size pixels and let the
		 * terminal scale to disp_cols x disp_rows cells.
		 * Avoids the "first downscale then upscale" blur:
		 * display_w/display_h were sized in pixels for a
		 * half-block protocol (~2px per cell), which would
		 * make the source extremely small before iTerm2
		 * stretches it back to full cells.
		 */
		disp_cols = display_w;
		disp_rows = (display_h + 1) / 2;
		if (disp_cols < 1)
			disp_cols = 1;
		if (disp_rows < 1)
			disp_rows = 1;

		display_pixels = arena_alloc(
			&ed->arenas.arenas[ARENA_COMMAND],
			(size_t)ed->img.width * ed->img.height *
			ed->img.channels);
		if (display_pixels) {
			memcpy(display_pixels, ed->img.pixels,
			       (size_t)ed->img.width * ed->img.height *
			       ed->img.channels);
			render_bboxes_on_pixels_raw(
				display_pixels, ed->img.width,
				ed->img.height, ed->img.channels,
				&ed->bboxes);
			img_data = render_image_iterm2(
				&ed->arenas.arenas[ARENA_COMMAND],
				display_pixels, ed->img.width,
				ed->img.height, ed->img.channels,
				disp_cols, disp_rows);
			if (img_data)
				tb_send(img_data, strlen(img_data));
		}
	} else {
		/* Sixel */
		display_pixels = image_resize(
			&ed->arenas.arenas[ARENA_COMMAND],
			ed->img.pixels, ed->img.width, ed->img.height,
			ed->img.channels, display_w, display_h);
		if (display_pixels) {
			render_bboxes_on_pixels_raw(
				display_pixels, display_w, display_h,
				ed->img.channels, &ed->bboxes);
			img_data = render_image_sixel(
				&ed->arenas.arenas[ARENA_COMMAND],
				display_pixels, display_w, display_h,
				ed->img.channels);
			if (img_data)
				tb_send(img_data, strlen(img_data));
		}
	}

	/* Second present: flush image data, no cell diffs */
	tb_present();

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

		if (ev->key == TB_KEY_MOUSE_LEFT) {
			if (ed->bboxes.drag_state == DRAG_NONE) {
				editor_push_history(ed, OP_ADD_BBOX);
				bbox_mouse_down(&ed->bboxes, px, py);
			} else {
				bbox_mouse_move(&ed->bboxes, px, py);
			}
			ed->mode = MODE_SELECT;
			ed->render.dirty = 1;
		} else if (ev->key == TB_KEY_MOUSE_RELEASE) {
			if (ed->bboxes.drag_state != DRAG_NONE) {
				int id;

				id = bbox_mouse_up(&ed->bboxes);
				if (id >= 0)
					ed->mode = MODE_CONFIRM;
				else
					ed->mode = MODE_VIEW;
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

		/* Drain all pending events before rendering */
		for (nev = 0; nev < 64; nev++) {
			ret = tb_peek_event(&ev, nev == 0 ? 30 : 0);
			if (ret != TB_OK)
				break;
			editor_handle_event(ed, &ev);
			if (!ed->running)
				break;
		}

		if (ed->render.dirty)
			editor_do_render(ed);
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
