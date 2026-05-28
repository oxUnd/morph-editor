#include "mask.h"
#include "arena.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_mask_from_bbox(void)
{
	struct arena a;
	struct bbox boxes[1];
	struct mask_options opts;
	unsigned char *mask;

	arena_init(&a);
	boxes[0].x = 10; boxes[0].y = 10;
	boxes[0].w = 20; boxes[0].h = 20;

	opts.feather = 0;
	opts.invert = 0;

	mask = mask_from_bboxes(&a, boxes, 1, 50, 50, opts);
	assert(mask != NULL);
	assert(mask[10 * 50 + 10] == 255);
	assert(mask[0] == 0);

	arena_free(&a);
	printf("  test_mask_from_bbox: PASS\n");
}

static void test_mask_invert(void)
{
	unsigned char mask[4];

	memset(mask, 0, 4);
	mask_invert(mask, 2, 2);
	assert(mask[0] == 255);
	printf("  test_mask_invert: PASS\n");
}

int main(void)
{
	printf("mask tests:\n");
	test_mask_from_bbox();
	test_mask_invert();
	printf("all mask tests passed!\n");
	return 0;
}
