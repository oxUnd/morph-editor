#ifndef MORPH_EDITOR_ARROW_H
#define MORPH_EDITOR_ARROW_H

#include <stdint.h>

#define ARROW_MAX 128
#define ARROW_LABEL_MAX 128

struct arrow_point {
	int image_index;
	int x, y;
};

struct arrow {
	int id;
	struct arrow_point from;
	struct arrow_point to;
	char label[ARROW_LABEL_MAX];
	uint32_t color;
	int label_editing;
};

enum arrow_drag_state {
	ARROW_DRAG_NONE = 0,
	ARROW_DRAG_FROM_SET = 1
};

struct arrow_manager {
	struct arrow arrows[ARROW_MAX];
	int count;
	int selected;
	int next_id;
	enum arrow_drag_state drag_state;
	struct arrow_point drag_from;
	int drag_cur_x, drag_cur_y;
};

void arrow_manager_init(struct arrow_manager *am);

int arrow_add(struct arrow_manager *am, struct arrow_point from,
	      struct arrow_point to, const char *label, uint32_t color);

int arrow_remove(struct arrow_manager *am, int id);

struct arrow *arrow_get_selected(struct arrow_manager *am);

void arrow_select_next(struct arrow_manager *am);

/*
 * arrow_find_near - find arrow id whose rendered line is near (tx, ty).
 * tx, ty are in terminal cell coordinates. Uses image_slots to convert
 * arrow endpoints to terminal coords, then computes point-to-segment
 * distance. Returns arrow index or -1.
 */
int arrow_find_near(struct arrow_manager *am, int tx, int ty,
		    int *canvas_offsets_x, int *canvas_offsets_y,
		    float *scales, int n_images, int threshold);

#endif /* MORPH_EDITOR_ARROW_H */
