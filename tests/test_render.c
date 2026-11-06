#include "arena.h"
#include "render.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	struct arena a;
	struct render_state rs;
	unsigned char pixels[4 * 4 * 4];
	char expect[64];
	char *out;

	memset(pixels, 0xff, sizeof(pixels));
	arena_init(&a);
	render_init(&rs, TERM_PROTO_KITTY);
	assert(rs.kitty_image_base > 0);

	out = render_image_kitty(&a, pixels, 4, 4, 4,
				 rs.kitty_image_base,
				 rs.kitty_placement_id, 4, 2);
	assert(out != NULL);
	snprintf(expect, sizeof(expect), "i=%d,p=%d",
		 rs.kitty_image_base, rs.kitty_placement_id);
	assert(strstr(out, expect) != NULL);
	assert(strncmp(out, "\033_Ga=T,f=100,", 13) == 0);

	arena_free(&a);
	printf("all render tests passed!\n");
	return 0;
}
