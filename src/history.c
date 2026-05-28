#include "history.h"

#include <stdlib.h>
#include <string.h>

void history_init(struct history_manager *hm, int max_depth)
{
	memset(hm, 0, sizeof(*hm));
	hm->max_depth = max_depth > 0 ? max_depth : 64;
	hm->undo_cap = 16;
	hm->redo_cap = 16;
	hm->undo_stack = malloc(hm->undo_cap * sizeof(struct history_op));
	hm->redo_stack = malloc(hm->redo_cap * sizeof(struct history_op));
}

void history_free(struct history_manager *hm)
{
	int i;

	if (hm->undo_stack) {
		for (i = 0; i < hm->undo_count; i++)
			cJSON_Delete(hm->undo_stack[i].snapshot);
		free(hm->undo_stack);
	}
	if (hm->redo_stack) {
		for (i = 0; i < hm->redo_count; i++)
			cJSON_Delete(hm->redo_stack[i].snapshot);
		free(hm->redo_stack);
	}
	hm->undo_stack = NULL;
	hm->redo_stack = NULL;
	hm->undo_count = 0;
	hm->redo_count = 0;
}

int history_push(struct history_manager *hm, enum op_type type,
		 cJSON *snapshot)
{
	struct history_op *op;

	if (hm->undo_count >= hm->undo_cap) {
		struct history_op *new_stack;

		hm->undo_cap *= 2;
		new_stack = realloc(hm->undo_stack,
				    hm->undo_cap * sizeof(struct history_op));
		if (!new_stack)
			return -1;
		hm->undo_stack = new_stack;
	}

	op = &hm->undo_stack[hm->undo_count++];
	op->type = type;
	op->snapshot = snapshot;

	/* Clear redo stack on new operation */
	{
		int i;

		for (i = 0; i < hm->redo_count; i++)
			cJSON_Delete(hm->redo_stack[i].snapshot);
		hm->redo_count = 0;
	}

	/* Enforce max depth */
	while (hm->undo_count > hm->max_depth) {
		cJSON_Delete(hm->undo_stack[0].snapshot);
		memmove(hm->undo_stack, hm->undo_stack + 1,
			(hm->undo_count - 1) * sizeof(struct history_op));
		hm->undo_count--;
	}

	return 0;
}

cJSON *history_undo(struct history_manager *hm, cJSON *current_state)
{
	struct history_op *op;
	struct history_op *new_redo;

	if (hm->undo_count <= 0)
		return NULL;

	op = &hm->undo_stack[--hm->undo_count];

	if (hm->redo_count >= hm->redo_cap) {
		struct history_op *new_stack;

		hm->redo_cap *= 2;
		new_stack = realloc(hm->redo_stack,
				    hm->redo_cap * sizeof(struct history_op));
		if (!new_stack) {
			/* Restore undo entry on failure */
			hm->undo_count++;
			return NULL;
		}
		hm->redo_stack = new_stack;
	}

	new_redo = &hm->redo_stack[hm->redo_count++];
	new_redo->type = op->type;
	new_redo->snapshot = current_state;

	return op->snapshot;
}

cJSON *history_redo(struct history_manager *hm, cJSON *current_state)
{
	struct history_op *op;
	struct history_op *new_undo;

	if (hm->redo_count <= 0)
		return NULL;

	op = &hm->redo_stack[--hm->redo_count];

	if (hm->undo_count >= hm->undo_cap) {
		struct history_op *new_stack;

		hm->undo_cap *= 2;
		new_stack = realloc(hm->undo_stack,
				    hm->undo_cap * sizeof(struct history_op));
		if (!new_stack) {
			hm->redo_count++;
			return NULL;
		}
		hm->undo_stack = new_stack;
	}

	new_undo = &hm->undo_stack[hm->undo_count++];
	new_undo->type = op->type;
	new_undo->snapshot = current_state;

	return op->snapshot;
}

int history_can_undo(struct history_manager *hm)
{
	return hm->undo_count > 0;
}

int history_can_redo(struct history_manager *hm)
{
	return hm->redo_count > 0;
}
