#include "bbox.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_init(void)
{
	struct bbox_manager bm;

	bbox_manager_init(&bm);
	assert(bm.count == 0);
	assert(bm.selected == -1);
	printf("  test_init: PASS\n");
}

static void test_add_remove(void)
{
	struct bbox_manager bm;
	int id;

	bbox_manager_init(&bm);
	id = bbox_add(&bm, 10, 20, 100, 80, "cat", 0xff0000);
	assert(id > 0);
	assert(bm.count == 1);
	assert(bm.selected == 0);
	assert(strcmp(bm.boxes[0].label, "cat") == 0);

	bbox_remove(&bm, id);
	assert(bm.count == 0);
	printf("  test_add_remove: PASS\n");
}

static void test_find_at(void)
{
	struct bbox_manager bm;

	bbox_manager_init(&bm);
	bbox_add(&bm, 10, 20, 100, 80, "a", 0);
	bbox_add(&bm, 200, 200, 50, 50, "b", 0);

	assert(bbox_find_at(&bm, 50, 50) >= 0);
	assert(bbox_find_at(&bm, 220, 220) >= 0);
	assert(bbox_find_at(&bm, 0, 0) < 0);
	printf("  test_find_at: PASS\n");
}

static void test_normalize(void)
{
	struct bbox b;

	b.x = 110; b.y = 100; b.w = -100; b.h = -80;
	bbox_normalize(&b);
	assert(b.x == 10);
	assert(b.y == 20);
	assert(b.w == 100);
	assert(b.h == 80);
	printf("  test_normalize: PASS\n");
}

static void test_select_next(void)
{
	struct bbox_manager bm;

	bbox_manager_init(&bm);
	bbox_add(&bm, 0, 0, 10, 10, "a", 0);
	bbox_add(&bm, 0, 0, 10, 10, "b", 0);

	/* After adding 2 bboxes, selected is on the last added (1) */
	assert(bm.selected == 1);
	bbox_select_next(&bm);
	assert(bm.selected == 0);
	bbox_select_next(&bm);
	assert(bm.selected == 1);
	printf("  test_select_next: PASS\n");
}

int main(void)
{
	printf("bbox tests:\n");
	test_init();
	test_add_remove();
	test_find_at();
	test_normalize();
	test_select_next();
	printf("all bbox tests passed!\n");
	return 0;
}
