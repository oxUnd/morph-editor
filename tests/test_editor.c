#include "editor.h"
#include "image.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void make_image(const char *path, int w, int h)
{
	unsigned char *pixels;
	int i;

	pixels = malloc((size_t)w * h * 4);
	assert(pixels);
	memset(pixels, 0, (size_t)w * h * 4);
	for (i = 0; i < w * h; i++)
		pixels[i * 4 + 3] = 255;
	assert(image_save(path, pixels, w, h, 4) == 0);
	free(pixels);
}

static void send_key(struct editor *ed, uint16_t key)
{
	struct tb_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = TB_EVENT_KEY;
	ev.key = key;
	editor_handle_event(ed, &ev);
}

static void send_key_mod(struct editor *ed, uint16_t key, uint8_t mod)
{
	struct tb_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = TB_EVENT_KEY;
	ev.key = key;
	ev.mod = mod;
	editor_handle_event(ed, &ev);
}

static void send_ch(struct editor *ed, uint32_t ch)
{
	struct tb_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = TB_EVENT_KEY;
	ev.ch = ch;
	editor_handle_event(ed, &ev);
}

int main(void)
{
	struct editor ed;
	const char *paths[2];
	struct arrow_point from;
	struct arrow_point to;
	char a[300];
	char b[300];

	snprintf(a, sizeof(a), "/tmp/morph_editor_%d_a.png",
		 (int)getpid());
	snprintf(b, sizeof(b), "/tmp/morph_editor_%d_b.png",
		 (int)getpid());
	make_image(a, 100, 100);
	make_image(b, 100, 100);

	editor_init(&ed);
	paths[0] = a;
	paths[1] = b;
	assert(editor_open_images(&ed, paths, 2) == 0);

	/* 'a' adds to the active image, not always image 0. */
	ed.active_image = 1;
	send_key(&ed, 'a');
	assert(ed.bboxes.count == 1);
	assert(ed.bboxes.boxes[0].image_index == 1);
	assert(ed.arrows.selected == -1);

	/* One undo removes one added box. */
	send_key(&ed, 'u');
	assert(ed.bboxes.count == 0);

	/* Two adds, one undo, one redo. No duplicate history entry. */
	send_key(&ed, 'a');
	send_key(&ed, 'a');
	assert(ed.bboxes.count == 2);
	send_key(&ed, 'u');
	assert(ed.bboxes.count == 1);
	send_key(&ed, 'r');
	assert(ed.bboxes.count == 2);

	send_key(&ed, 'd');
	assert(ed.bboxes.count == 1);

	/*
	 * A selected bbox must clear a stale arrow selection, so 'd'
	 * deletes the bbox and not the arrow.
	 */
	from.image_index = 0;
	from.x = 0;
	from.y = 0;
	to.image_index = 0;
	to.x = 10;
	to.y = 10;
	assert(arrow_add(&ed.arrows, from, to, NULL, 0) > 0);
	assert(ed.arrows.count == 1);
	send_key(&ed, 'a');
	assert(ed.bboxes.count == 2);
	assert(ed.arrows.selected == -1);
	send_key(&ed, 'd');
	assert(ed.bboxes.count == 1);
	assert(ed.arrows.count == 1);

	/* Tab cycles bboxes first, then arrows, clearing the other. */
	ed.bboxes.selected = 0;
	ed.arrows.selected = -1;
	send_key(&ed, TB_KEY_TAB);
	assert(ed.arrows.selected == 0);
	assert(ed.bboxes.selected == -1);
	send_key(&ed, TB_KEY_TAB);
	assert(ed.bboxes.selected == 0);
	assert(ed.arrows.selected == -1);

	/* Nudge and resize go through the transaction path. */
	{
		int x0;

		x0 = ed.bboxes.boxes[0].x;
		send_key(&ed, TB_KEY_ARROW_RIGHT);
		assert(ed.bboxes.boxes[0].x == x0 + 1);
		send_key_mod(&ed, TB_KEY_ARROW_RIGHT, TB_MOD_SHIFT);
		assert(ed.bboxes.boxes[0].x == x0 + 11);
	}

	/* Help overlay is a simple toggle. */
	send_key(&ed, '?');
	assert(ed.show_help == 1);
	send_key(&ed, TB_KEY_ESC);
	assert(ed.show_help == 0);

	/* Label editing supports a cursor in the middle of the text. */
	ed.bboxes.selected = 0;
	ed.arrows.selected = -1;
	send_key(&ed, 'e');
	assert(ed.mode == MODE_LABEL_EDIT);
	send_ch(&ed, 'a');
	send_ch(&ed, 'b');
	send_key(&ed, TB_KEY_ARROW_LEFT);
	send_ch(&ed, 'c');
	assert(strcmp(ed.label_buf, "acb") == 0);
	send_key(&ed, TB_KEY_BACKSPACE);
	assert(strcmp(ed.label_buf, "ab") == 0);
	send_key(&ed, TB_KEY_ENTER);
	assert(ed.mode == MODE_VIEW);
	assert(strcmp(ed.bboxes.boxes[0].label, "ab") == 0);

	/* Protocol override validation. */
	assert(editor_set_proto(&ed, "kitty") == 0);
	assert(ed.forced_proto == TERM_PROTO_KITTY);
	assert(editor_set_proto(&ed, "bogus") < 0);
	assert(editor_set_proto(&ed, "auto") == 0);
	assert(ed.forced_proto == -1);

	/* Annotation file round trip (the commit above autosaves). */
	{
		char ann[300];
		struct editor ed2;

		snprintf(ann, sizeof(ann), "/tmp/morph_editor_%d.json",
			 (int)getpid());
		editor_set_annotation_path(&ed, ann);
		send_key(&ed, 'a');
		assert(file_exists(ann));

		editor_init(&ed2);
		assert(editor_open_images(&ed2, paths, 2) == 0);
		assert(editor_load_annotations(&ed2, ann) == 0);
		assert(ed2.bboxes.count == ed.bboxes.count);
		assert(ed2.arrows.count == ed.arrows.count);
		editor_free(&ed2);
		remove(ann);
		editor_set_annotation_path(&ed, NULL);
	}

	editor_free(&ed);
	remove(a);
	remove(b);
	printf("all editor tests passed!\n");
	return 0;
}
