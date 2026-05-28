#include "draw.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_draw_rect(void)
{
	unsigned char pixels[20 * 20 * 4];
	struct draw_command cmd;

	memset(pixels, 0, sizeof(pixels));
	cmd.type = DRAW_RECT;
	cmd.x = 5; cmd.y = 5; cmd.w = 10; cmd.h = 10;
	cmd.color = 0xff0000;
	cmd.thickness = 1;

	draw_execute(pixels, 20, 20, 4, &cmd);
	/* Check corner pixel is set */
	assert(pixels[(5 * 20 + 5) * 4] == 0xff);
	printf("  test_draw_rect: PASS\n");
}

static void test_draw_line(void)
{
	unsigned char pixels[20 * 20 * 4];

	memset(pixels, 0, sizeof(pixels));
	draw_line(pixels, 20, 20, 4, 0, 0, 19, 19, 0x00ff00, 1);
	assert(pixels[0] == 0x00);
	assert(pixels[1] == 0xff);
	printf("  test_draw_line: PASS\n");
}

int main(void)
{
	printf("draw tests:\n");
	test_draw_rect();
	test_draw_line();
	printf("all draw tests passed!\n");
	return 0;
}
