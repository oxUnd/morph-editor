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
	MODE_CONFIRM
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
