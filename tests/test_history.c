#include "history.h"
#include <stdio.h>
#include <assert.h>

static void test_push_undo(void)
{
	struct history_manager hm;
	cJSON *snap;

	history_init(&hm, 10);

	snap = cJSON_CreateObject();
	cJSON_AddStringToObject(snap, "test", "data");
	history_push(&hm, OP_ADD_BBOX, snap);

	assert(history_can_undo(&hm));
	assert(!history_can_redo(&hm));

	history_free(&hm);
	printf("  test_push_undo: PASS\n");
}

static void test_undo_redo(void)
{
	struct history_manager hm;
	cJSON *snap1, *snap2, *cur;
	cJSON *result;

	history_init(&hm, 10);

	snap1 = cJSON_CreateObject();
	cJSON_AddNumberToObject(snap1, "id", 1);
	history_push(&hm, OP_ADD_BBOX, snap1);

	snap2 = cJSON_CreateObject();
	cJSON_AddNumberToObject(snap2, "id", 2);
	history_push(&hm, OP_ADD_BBOX, snap2);

	/* Undo: pass current state, get back the previous snapshot */
	cur = cJSON_CreateObject();
	cJSON_AddNumberToObject(cur, "id", 3);
	result = history_undo(&hm, cur);
	assert(result != NULL);
	cJSON_Delete(result);

	/* Now redo should work */
	assert(history_can_redo(&hm));
	cur = cJSON_CreateObject();
	cJSON_AddNumberToObject(cur, "id", 4);
	result = history_redo(&hm, cur);
	assert(result != NULL);
	cJSON_Delete(result);

	history_free(&hm);
	printf("  test_undo_redo: PASS\n");
}

static void test_max_depth(void)
{
	struct history_manager hm;
	int i;

	history_init(&hm, 3);

	for (i = 0; i < 5; i++) {
		cJSON *snap;

		snap = cJSON_CreateObject();
		cJSON_AddNumberToObject(snap, "i", i);
		history_push(&hm, OP_DRAW, snap);
	}

	/* Should only have 3 items in undo stack */
	assert(hm.undo_count <= 3);

	history_free(&hm);
	printf("  test_max_depth: PASS\n");
}

int main(void)
{
	printf("history tests:\n");
	test_push_undo();
	test_undo_redo();
	test_max_depth();
	printf("all history tests passed!\n");
	return 0;
}
