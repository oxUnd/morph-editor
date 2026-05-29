#ifndef MORPH_EDITOR_HISTORY_H
#define MORPH_EDITOR_HISTORY_H

#include "cJSON.h"

enum op_type {
	OP_ADD_BBOX,
	OP_REMOVE_BBOX,
	OP_MOVE_BBOX,
	OP_RESIZE_BBOX,
	OP_EDIT_LABEL,
	OP_DRAW,
	OP_TRANSFORM,
	OP_COMPOSITE,
	OP_ADD_ARROW,
	OP_REMOVE_ARROW,
	OP_EDIT_ARROW_LABEL
};

struct history_op {
	enum op_type type;
	cJSON *snapshot;
};

struct history_manager {
	struct history_op *undo_stack;
	struct history_op *redo_stack;
	int undo_count;
	int redo_count;
	int undo_cap;
	int redo_cap;
	int max_depth;
};

/*
 * history_init - initialize history manager
 */
void history_init(struct history_manager *hm, int max_depth);

/*
 * history_free - free history manager resources
 */
void history_free(struct history_manager *hm);

/*
 * history_push - push a "before" state snapshot onto the undo stack.
 * The history manager takes ownership of `snapshot`.
 */
int history_push(struct history_manager *hm, enum op_type type,
		 cJSON *snapshot);

/*
 * history_undo - swap current state for the previous one.
 * Caller passes the current state (ownership transferred to history,
 * which moves it to the redo stack). Returns the snapshot to apply,
 * which the caller must `cJSON_Delete` after restoring.
 * Returns NULL if no undo is available; in that case `current_state`
 * is left untouched and the caller still owns it.
 */
cJSON *history_undo(struct history_manager *hm, cJSON *current_state);

/*
 * history_redo - swap current state for the next one.
 * Same ownership rules as history_undo.
 */
cJSON *history_redo(struct history_manager *hm, cJSON *current_state);

/*
 * history_can_undo - check if undo is possible
 */
int history_can_undo(struct history_manager *hm);

/*
 * history_can_redo - check if redo is possible
 */
int history_can_redo(struct history_manager *hm);

#endif /* MORPH_EDITOR_HISTORY_H */
