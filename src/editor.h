#ifndef MORPH_EDITOR_EDITOR_H
#define MORPH_EDITOR_EDITOR_H

#include "arena.h"
#include "image.h"
#include "render.h"
#include "bbox.h"
#include "arrow.h"
#include "history.h"
#include "terminal.h"
#include "layout.h"

#include "termbox2.h"

enum editor_mode {
	MODE_VIEW,
	MODE_SELECT,
	MODE_CONFIRM,
	MODE_LABEL_EDIT,
	MODE_ARROW_DRAW
};

struct image_slot {
	struct image img;
	int canvas_x, canvas_y;
	int display_w, display_h;
	float scale;
};

struct editor {
	struct arena_set arenas;
	struct image_slot images[MAX_IMAGES];
	int image_count;
	struct render_state render;
	struct bbox_manager bboxes;
	struct arrow_manager arrows;
	struct history_manager history;
	enum term_proto proto;
	enum editor_mode mode;
	int running;
	int undo_count;
	long long last_render_ms;
	char label_buf[ARROW_LABEL_MAX];
	int label_len;
	int label_target_id;
	int label_is_arrow;
	int drag_image_index;
	int sidebar_row_to_id[256];
	int sidebar_row_count;
	int image_uploaded;
	int image_gen;
	int last_uploaded_gen;
	int last_term_w;
	int last_term_h;
	unsigned long last_arrows_fp;
	int force_full_render;
	struct layout_slot layout_slots[MAX_IMAGES];
};

int editor_init(struct editor *ed);
void editor_free(struct editor *ed);
int editor_open_images(struct editor *ed, const char **paths, int count);
int editor_open_image_base64(struct editor *ed, const char *b64);
int editor_run(struct editor *ed);
void editor_stop(struct editor *ed);
void editor_render(struct editor *ed);
void editor_handle_event(struct editor *ed, struct tb_event *ev);

#endif /* MORPH_EDITOR_EDITOR_H */
