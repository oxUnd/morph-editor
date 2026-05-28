#ifndef MORPH_EDITOR_BBOX_H
#define MORPH_EDITOR_BBOX_H

#include <stdint.h>

#define BBOX_MAX 256
#define BBOX_LABEL_MAX 64

struct bbox {
	int x, y, w, h;
	char label[BBOX_LABEL_MAX];
	uint32_t color;
	int id;
};

#define DRAG_NONE 0
#define DRAG_CREATE 1
#define DRAG_MOVE 2
#define DRAG_RESIZE 3

#define HANDLE_NONE 0
#define HANDLE_TL 1
#define HANDLE_TR 2
#define HANDLE_BL 3
#define HANDLE_BR 4
#define HANDLE_BODY 5

struct bbox_manager {
	struct bbox boxes[BBOX_MAX];
	int count;
	int selected;
	int next_id;
	int drag_state;
	int drag_handle;
	int start_x, start_y;
	int cur_x, cur_y;
	int orig_x, orig_y, orig_w, orig_h;
};

/*
 * bbox_manager_init - initialize bbox manager
 */
void bbox_manager_init(struct bbox_manager *bm);

/*
 * bbox_add - add a new bbox, returns its id or negative on error
 */
int bbox_add(struct bbox_manager *bm, int x, int y, int w, int h,
	     const char *label, uint32_t color);

/*
 * bbox_remove - remove bbox by id
 */
int bbox_remove(struct bbox_manager *bm, int id);

/*
 * bbox_find_at - find bbox id at pixel coordinates, -1 if none
 */
int bbox_find_at(struct bbox_manager *bm, int px, int py);

/*
 * bbox_hit_handle - detect which resize handle is at (px, py)
 * Returns HANDLE_* constant.
 */
int bbox_hit_handle(struct bbox *b, int px, int py, int handle_size);

/*
 * bbox_mouse_down - handle mouse press at pixel coordinates
 */
void bbox_mouse_down(struct bbox_manager *bm, int px, int py);

/*
 * bbox_mouse_move - handle mouse drag at pixel coordinates
 */
void bbox_mouse_move(struct bbox_manager *bm, int px, int py);

/*
 * bbox_mouse_up - handle mouse release
 * Returns the id of the newly created bbox, or -1.
 */
int bbox_mouse_up(struct bbox_manager *bm);

/*
 * bbox_select_next - cycle selection to next bbox
 */
void bbox_select_next(struct bbox_manager *bm);

/*
 * bbox_normalize - ensure w,h are positive (flip if needed)
 */
void bbox_normalize(struct bbox *b);

/*
 * bbox_get_selected - get pointer to currently selected bbox
 * Returns NULL if no selection.
 */
struct bbox *bbox_get_selected(struct bbox_manager *bm);

#endif /* MORPH_EDITOR_BBOX_H */
