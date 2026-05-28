#include "image.h"
#include "arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_info_nonexistent(void)
{
	char *info;

	info = image_info_json("/nonexistent/file.jpg");
	assert(info == NULL);
	printf("  test_info_nonexistent: PASS\n");
}

static void test_save_load_roundtrip(void)
{
	struct arena a;
	unsigned char pixels[4 * 4 * 4]; /* 4x4 RGBA */
	struct image img;
	int i;

	arena_init(&a);

	/* Create a simple test image */
	for (i = 0; i < 16; i++) {
		pixels[i * 4] = (unsigned char)(i * 16);
		pixels[i * 4 + 1] = (unsigned char)(255 - i * 16);
		pixels[i * 4 + 2] = 128;
		pixels[i * 4 + 3] = 255;
	}

	/* Save as PNG */
	assert(image_save("/tmp/morph_test.png",
			  pixels, 4, 4, 4) == 0);

	/* Load it back */
	assert(image_load(&a, "/tmp/morph_test.png", &img) == 0);
	assert(img.width == 4);
	assert(img.height == 4);
	assert(img.channels == 4);

	arena_free(&a);
	printf("  test_save_load_roundtrip: PASS\n");
}

static void test_resize(void)
{
	struct arena a;
	unsigned char src[2 * 2 * 4];
	unsigned char *dst;

	arena_init(&a);

	memset(src, 255, sizeof(src));
	dst = image_resize(&a, src, 2, 2, 4, 4, 4);
	assert(dst != NULL);

	arena_free(&a);
	printf("  test_resize: PASS\n");
}

int main(void)
{
	printf("image tests:\n");
	test_info_nonexistent();
	test_save_load_roundtrip();
	test_resize();
	printf("all image tests passed!\n");
	return 0;
}
