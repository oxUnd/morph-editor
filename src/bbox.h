#ifndef MORPH_EDITOR_BBOX_H
#define MORPH_EDITOR_BBOX_H

#include <stdint.h>

#define BBOX_MAX 256
#define BBOX_LABEL_MAX 64
#define BBOX_KF_MAX 64

struct bbox_kf {
	int frame;
	int x, y, w, h;
};

struct bbox {
	int x, y, w, h;
	int image_index;
	char label[BBOX_LABEL_MAX];
	uint32_t color;
	int id;
	/*
	 * keyframes[] is kept sorted by frame ascending. When
	 * kf_count > 0 (video mode), x/y/w/h above are the
	 * interpolated values for the editor's current frame and
	 * are recomputed by bbox_apply_frame().
	 */
	int kf_count;
	struct bbox_kf keyframes[BBOX_KF_MAX];
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
	     int image_index, const char *label, uint32_t color);

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
 * bbox_mouse_down - handle mouse press at pixel coordinates.
 *
 * `button` selects the interaction:
 *   BBOX_BTN_PRIMARY (left): always start creating a new bbox.
 *   BBOX_BTN_SECONDARY (right): try to select / move / resize an
 *       existing bbox. Falls through to nothing if no hit.
 *
 * Splitting left and right resolves the conflict between
 * "draw new" and "select existing" when both used the same
 * left button.
 */
#define BBOX_BTN_PRIMARY    1
#define BBOX_BTN_SECONDARY  2

void bbox_mouse_down(struct bbox_manager *bm, int px, int py,
		     int button);

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

/*
 * bbox_set_keyframe - record a keyframe at `frame` for the bbox with
 * the given id. If a keyframe at that frame already exists it is
 * overwritten. The bbox's live x/y/w/h fields are also updated to
 * the same values. Returns 0 on success, -1 on error.
 */
int bbox_set_keyframe(struct bbox_manager *bm, int id, int frame,
		      int x, int y, int w, int h);

/*
 * bbox_remove_keyframe - remove a keyframe at `frame` for the bbox
 * with the given id. Returns 0 if removed, -1 if not found. After
 * removal, callers may want to remove the bbox itself if kf_count
 * drops to 0.
 */
int bbox_remove_keyframe(struct bbox_manager *bm, int id, int frame);

/*
 * bbox_apply_frame - for every bbox with kf_count > 0, recompute
 * x/y/w/h by linear interpolation between the two keyframes
 * straddling `frame`. Out-of-range frames clamp to the nearest
 * keyframe.
 */
void bbox_apply_frame(struct bbox_manager *bm, int frame);

#endif /* MORPH_EDITOR_BBOX_H */
