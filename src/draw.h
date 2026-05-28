#ifndef MORPH_EDITOR_DRAW_H
#define MORPH_EDITOR_DRAW_H

#include <stdint.h>

enum draw_type {
	DRAW_RECT,
	DRAW_FILLED_RECT,
	DRAW_CIRCLE,
	DRAW_FILLED_CIRCLE,
	DRAW_LINE,
	DRAW_ARROW,
	DRAW_TEXT,
	DRAW_FILL
};

struct draw_command {
	enum draw_type type;
	int x, y, w, h;
	char text[256];
	uint32_t color;
	int thickness;
	int font_size;
};

/*
 * draw_execute - execute a draw command on pixel buffer
 * Modifies pixels in-place. Returns 0 on success.
 */
int draw_execute(unsigned char *pixels, int w, int h, int channels,
		 struct draw_command *cmd);

/*
 * draw_line - draw a line between two points
 */
void draw_line(unsigned char *pixels, int w, int h, int channels,
	       int x0, int y0, int x1, int y1,
	       uint32_t color, int thickness);

/*
 * draw_circle - draw circle outline
 */
void draw_circle(unsigned char *pixels, int w, int h, int channels,
		 int cx, int cy, int radius, uint32_t color,
		 int thickness);

/*
 * draw_filled_circle - draw filled circle
 */
void draw_filled_circle(unsigned char *pixels, int w, int h, int channels,
			int cx, int cy, int radius, uint32_t color);

/*
 * draw_arrow - draw arrow from (x,y) to (x+w, y+h)
 */
void draw_arrow(unsigned char *pixels, int w, int h, int channels,
		int x0, int y0, int x1, int y1,
		uint32_t color, int thickness);

/*
 * draw_text_on_pixels - render text onto pixel buffer
 * Uses stb_truetype if a font is available, else stb_easy_font.
 */
void draw_text_on_pixels(unsigned char *pixels, int w, int h, int channels,
			 int x, int y, const char *text,
			 uint32_t color, int font_size);

/*
 * draw_flood_fill - flood fill at point with color
 */
void draw_flood_fill(unsigned char *pixels, int w, int h, int channels,
		     int x, int y, uint32_t color);

#endif /* MORPH_EDITOR_DRAW_H */
