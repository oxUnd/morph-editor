#include "transform.h"
#include "arena.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_flip_horizontal(void)
{
	struct arena a;
	unsigned char pixels[4 * 2 * 4]; /* 4x2 RGBA */
	unsigned char *result;
	int w, h, channels;
	struct transform_options opts;

	arena_init(&a);

	/* Create test: top-left red, top-right blue */
	memset(pixels, 0, sizeof(pixels));
	pixels[0] = 255; /* R at (0,0) */
	pixels[(3 * 4) + 2] = 255; /* B at (3,0) */

	w = 4; h = 2; channels = 4;
	result = pixels;
	opts.resize_w = 0; opts.resize_h = 0;
	opts.rotate = 0;
	opts.flip_h = 1; opts.flip_v = 0;
	opts.pad_w = 0; opts.pad_h = 0;

	{
		unsigned char *p = result;
		int r;

		r = transform_apply(&a, &p, &w, &h, channels, opts);
		assert(r == 0);
		/* After flip, old (3,0) blue should be at (0,0) */
		assert(p[2] == 255);
	}

	arena_free(&a);
	printf("  test_flip_horizontal: PASS\n");
}

static void test_resize(void)
{
	struct arena a;
	unsigned char pixels[4 * 4 * 4];
	unsigned char *result;
	int w, h, channels;
	struct transform_options opts;

	arena_init(&a);

	memset(pixels, 128, sizeof(pixels));
	w = 4; h = 4; channels = 4;
	result = pixels;

	opts.resize_w = 8; opts.resize_h = 8;
	opts.rotate = 0;
	opts.flip_h = 0; opts.flip_v = 0;
	opts.pad_w = 0; opts.pad_h = 0;

	{
		unsigned char *p = result;
		int r;

		r = transform_apply(&a, &p, &w, &h, channels, opts);
		assert(r == 0);
		assert(w == 8);
		assert(h == 8);
	}

	arena_free(&a);
	printf("  test_resize: PASS\n");
}

int main(void)
{
	printf("transform tests:\n");
	test_flip_horizontal();
	test_resize();
	printf("all transform tests passed!\n");
	return 0;
}
