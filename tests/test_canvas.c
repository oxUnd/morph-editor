#include "canvas.h"
#include "arena.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_composite_single_layer(void)
{
	struct arena a;
	struct canvas_layer layer;
	unsigned char *result;
	unsigned char pixels[2 * 2 * 4];

	arena_init(&a);

	memset(pixels, 255, sizeof(pixels));
	layer.image_path = NULL;
	layer.pixels = pixels;
	layer.w = 2; layer.h = 2;
	layer.x = 0; layer.y = 0;
	layer.opacity = 1.0f;
	layer.blend = BLEND_NORMAL;

	result = canvas_composite(&a, 4, 4, &layer, 1, 0x000000);
	assert(result != NULL);
	assert(result[0] == 255); /* White pixel */

	arena_free(&a);
	printf("  test_composite_single_layer: PASS\n");
}

int main(void)
{
	printf("canvas tests:\n");
	test_composite_single_layer();
	printf("all canvas tests passed!\n");
	return 0;
}
