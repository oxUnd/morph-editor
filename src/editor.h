#ifndef MORPH_EDITOR_EDITOR_H
#define MORPH_EDITOR_EDITOR_H

#include "arena.h"
#include "image.h"
#include "render.h"
#include "bbox.h"
#include "history.h"
#include "terminal.h"

#include "termbox2.h"

enum editor_mode {
	MODE_VIEW,
	MODE_SELECT,
	MODE_CONFIRM,
	MODE_LABEL_EDIT
};

struct editor {
	struct arena_set arenas;
	struct image img;
	struct render_state render;
	struct bbox_manager bboxes;
	struct history_manager history;
	enum term_proto proto;
	enum editor_mode mode;
	int running;
	int undo_count;
	long long last_render_ms;
	char label_buf[BBOX_LABEL_MAX];
	int label_len;
	int label_target_id;
	/*
	 * sidebar_row_to_id[row] -> bbox id at that terminal
	 * row in the sidebar list (or -1 if no bbox there).
	 * Updated each time the sidebar is drawn so that
	 * mouse clicks in the sidebar can be mapped back
	 * to a bbox id.
	 */
	int sidebar_row_to_id[256];
	int sidebar_row_count;
	/*
	 * Layered rendering: the base image is uploaded once
	 * (via Kitty/iTerm2/Sixel) and then cached. bbox
	 * outlines are drawn on top using termbox cells
	 * (the "overlay layer"), so dragging/resizing a bbox
	 * only repaints a few cells instead of re-encoding
	 * and re-transmitting the whole image. Bumping the
	 * image generation forces a full re-upload on the
	 * next render.
	 */
	int image_uploaded;
	int image_gen;
	int last_uploaded_gen;
	int last_term_w;
	int last_term_h;
};

int editor_init(struct editor *ed);
void editor_free(struct editor *ed);
int editor_open_image(struct editor *ed, const char *path);
int editor_open_image_base64(struct editor *ed, const char *b64);
int editor_run(struct editor *ed);
void editor_stop(struct editor *ed);
void editor_render(struct editor *ed);
void editor_handle_event(struct editor *ed, struct tb_event *ev);

#endif /* MORPH_EDITOR_EDITOR_H */
