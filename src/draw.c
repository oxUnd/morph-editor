#include "draw.h"
#include "render.h"
#include "util.h"

#define STB_TRUETYPE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_truetype.h"
#include "stb_easy_font.h"
#pragma GCC diagnostic pop

#include <stdlib.h>
#include <string.h>
#include <math.h>

static void set_pixel_safe(unsigned char *pixels, int w, int h, int channels,
			   int x, int y, uint32_t color)
{
	unsigned char *p;

	if (x < 0 || x >= w || y < 0 || y >= h)
		return;
	p = pixels + (y * w + x) * channels;
	p[0] = COLOR_R(color);
	p[1] = COLOR_G(color);
	p[2] = COLOR_B(color);
	if (channels == 4)
		p[3] = 255;
}

void draw_line(unsigned char *pixels, int w, int h, int channels,
	       int x0, int y0, int x1, int y1,
	       uint32_t color, int thickness)
{
	int dx, dy, sx, sy, err, e2;
	int i, t;

	dx = abs(x1 - x0);
	dy = -abs(y1 - y0);
	sx = x0 < x1 ? 1 : -1;
	sy = y0 < y1 ? 1 : -1;
	err = dx + dy;

	for (;;) {
		for (t = 0; t < thickness; t++) {
			for (i = 0; i < thickness; i++)
				set_pixel_safe(pixels, w, h, channels,
					       x0 + t, y0 + i, color);
		}
		if (x0 == x1 && y0 == y1)
			break;
		e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

void draw_circle(unsigned char *pixels, int w, int h, int channels,
		 int cx, int cy, int radius, uint32_t color,
		 int thickness)
{
	int x, y, dx, dy;

	for (y = -radius; y <= radius; y++) {
		for (x = -radius; x <= radius; x++) {
			dx = x * x + y * y;
			dy = (radius - thickness) * (radius - thickness);
			if (dx <= radius * radius && dx >= dy)
				set_pixel_safe(pixels, w, h, channels,
					       cx + x, cy + y, color);
		}
	}
}

void draw_filled_circle(unsigned char *pixels, int w, int h, int channels,
			int cx, int cy, int radius, uint32_t color)
{
	int x, y;

	for (y = -radius; y <= radius; y++) {
		for (x = -radius; x <= radius; x++) {
			if (x * x + y * y <= radius * radius)
				set_pixel_safe(pixels, w, h, channels,
					       cx + x, cy + y, color);
		}
	}
}

void draw_arrow(unsigned char *pixels, int w, int h, int channels,
		int x0, int y0, int x1, int y1,
		uint32_t color, int thickness)
{
	double angle;
	double a_angle, b_angle;
	int ax, ay, bx, by;
	int head_len;

	draw_line(pixels, w, h, channels, x0, y0, x1, y1,
		  color, thickness);

	head_len = 10;
	angle = atan2(y1 - y0, x1 - x0);
	a_angle = angle + M_PI * 0.8;
	b_angle = angle - M_PI * 0.8;

	ax = x1 + (int)(head_len * cos(a_angle));
	ay = y1 + (int)(head_len * sin(a_angle));
	bx = x1 + (int)(head_len * cos(b_angle));
	by = y1 + (int)(head_len * sin(b_angle));

	draw_line(pixels, w, h, channels, x1, y1, ax, ay,
		  color, thickness);
	draw_line(pixels, w, h, channels, x1, y1, bx, by,
		  color, thickness);
}

void draw_text_on_pixels(unsigned char *pixels, int w, int h, int channels,
			 int x, int y, const char *text,
			 uint32_t color, int font_size)
{
	unsigned char *bitmap;
	int text_w;
	int i, j;
	unsigned char val;

	if (!text || !text[0])
		return;

	/* Use stb_easy_font for simple bitmap text */
	text_w = strlen(text) * font_size + 4;
	bitmap = calloc(text_w, (size_t)font_size + 4);
	if (!bitmap)
		return;

	stb_easy_font_spacing(font_size / 4);
	{
		char *mutable_text;

		/* stb_easy_font_print takes non-const text */
		mutable_text = (char *)text;
		stb_easy_font_print(0, 0, mutable_text, NULL,
				    bitmap, text_w);
	}

	for (j = 0; j < font_size + 2; j++) {
		for (i = 0; i < text_w; i++) {
			val = bitmap[j * text_w + i];
			if (val > 128)
				set_pixel_safe(pixels, w, h, channels,
					       x + i, y + j, color);
		}
	}
	free(bitmap);
}

void draw_flood_fill(unsigned char *pixels, int w, int h, int channels,
		     int x, int y, uint32_t color)
{
	uint32_t target;
	int *stack;
	int stack_cap;
	int stack_top;
	int cx, cy;
	unsigned char *p;

	if (x < 0 || x >= w || y < 0 || y >= h)
		return;

	p = pixels + (y * w + x) * channels;
	target = COLOR_RGB(p[0], p[1], p[2]);
	if (target == color)
		return;

	stack_cap = w * h / 4 + 1024;
	stack = malloc(stack_cap * 2 * sizeof(int));
	if (!stack)
		return;
	stack_top = 0;

	stack[stack_top * 2] = x;
	stack[stack_top * 2 + 1] = y;
	stack_top++;

	while (stack_top > 0) {
		stack_top--;
		cx = stack[stack_top * 2];
		cy = stack[stack_top * 2 + 1];

		if (cx < 0 || cx >= w || cy < 0 || cy >= h)
			continue;

		p = pixels + (cy * w + cx) * channels;
		if (COLOR_RGB(p[0], p[1], p[2]) != target)
			continue;

		p[0] = COLOR_R(color);
		p[1] = COLOR_G(color);
		p[2] = COLOR_B(color);
		if (channels == 4)
			p[3] = 255;

		if (stack_top + 4 >= stack_cap) {
			stack_cap *= 2;
			{
				int *new_stack;

				new_stack = realloc(stack,
						    stack_cap * 2 *
						    sizeof(int));
				if (!new_stack)
					break;
				stack = new_stack;
			}
		}

		stack[stack_top * 2] = cx + 1;
		stack[stack_top * 2 + 1] = cy;
		stack_top++;
		stack[stack_top * 2] = cx - 1;
		stack[stack_top * 2 + 1] = cy;
		stack_top++;
		stack[stack_top * 2] = cx;
		stack[stack_top * 2 + 1] = cy + 1;
		stack_top++;
		stack[stack_top * 2] = cx;
		stack[stack_top * 2 + 1] = cy - 1;
		stack_top++;
	}

	free(stack);
}

int draw_execute(unsigned char *pixels, int w, int h, int channels,
		 struct draw_command *cmd)
{
	switch (cmd->type) {
	case DRAW_RECT:
		draw_rect_outline(pixels, w, h, channels,
				  cmd->x, cmd->y, cmd->w, cmd->h,
				  cmd->color, cmd->thickness);
		break;
	case DRAW_FILLED_RECT: {
		int i, j;
		for (j = cmd->y; j < cmd->y + cmd->h; j++)
			for (i = cmd->x; i < cmd->x + cmd->w; i++)
				set_pixel_safe(pixels, w, h, channels,
					       i, j, cmd->color);
		break;
	}
	case DRAW_CIRCLE:
		draw_circle(pixels, w, h, channels,
			    cmd->x, cmd->y, cmd->w, cmd->color,
			    cmd->thickness);
		break;
	case DRAW_FILLED_CIRCLE:
		draw_filled_circle(pixels, w, h, channels,
				   cmd->x, cmd->y, cmd->w, cmd->color);
		break;
	case DRAW_LINE:
		draw_line(pixels, w, h, channels,
			  cmd->x, cmd->y, cmd->x + cmd->w,
			  cmd->y + cmd->h, cmd->color, cmd->thickness);
		break;
	case DRAW_ARROW:
		draw_arrow(pixels, w, h, channels,
			   cmd->x, cmd->y, cmd->x + cmd->w,
			   cmd->y + cmd->h, cmd->color, cmd->thickness);
		break;
	case DRAW_TEXT:
		draw_text_on_pixels(pixels, w, h, channels,
				    cmd->x, cmd->y, cmd->text,
				    cmd->color, cmd->font_size ?
				    cmd->font_size : 16);
		break;
	case DRAW_FILL:
		draw_flood_fill(pixels, w, h, channels,
				cmd->x, cmd->y, cmd->color);
		break;
	}
	return 0;
}
