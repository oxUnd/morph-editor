#ifndef MORPH_EDITOR_RENDER_H
#define MORPH_EDITOR_RENDER_H

#include <stdint.h>
#include "arena.h"
#include "terminal.h"
#include "image.h"
#include "bbox.h"

#define SIDEBAR_W 32

struct render_state {
	enum term_proto proto;
	int term_w;
	int term_h;
	int img_display_w;
	int img_display_h;
	int offset_x;
	int offset_y;
	float scale;
	int kitty_placement_id;
	int dirty;
	int sidebar_w;
};

void render_init(struct render_state *rs, enum term_proto proto);
void render_calc_fit(struct render_state *rs, int img_w, int img_h,
		     int term_w, int term_h);

void term_to_pixel(int tx, int ty, int *px, int *py,
		   int img_w, int img_h,
		   int offset_x, int offset_y, float scale);

void pixel_to_term(int px, int py, int *tx, int *ty,
		   int offset_x, int offset_y, float scale);

char *render_image_sixel(struct arena *a, const unsigned char *pixels,
			 int w, int h, int channels);

char *render_image_kitty(struct arena *a, const unsigned char *pixels,
			 int w, int h, int channels, int placement_id,
			 int disp_cols, int disp_rows);

char *render_image_iterm2(struct arena *a, const unsigned char *pixels,
			  int w, int h, int channels,
			  int disp_cols, int disp_rows);

void render_bboxes_on_pixels(unsigned char *pixels, int w, int h,
			     int channels, struct bbox_manager *bm,
			     float scale, int offset_x, int offset_y);

void render_bboxes_on_pixels_raw(unsigned char *pixels, int w, int h,
				 int channels, struct bbox_manager *bm);

void draw_rect_outline(unsigned char *pixels, int w, int h, int channels,
		       int rx, int ry, int rw, int rh,
		       uint32_t color, int thickness);

void render_status_bar(const char *filename, int img_w, int img_h,
		       struct bbox_manager *bm, const char *mode,
		       int undo_count, int term_w,
		       int img_rows);

void clear_screen(void);
void move_cursor(int row, int col);
void sixel_write(const char *data, int len);
void kitty_write(const char *data, int len);
void kitty_delete_placement(int placement_id);

#endif /* MORPH_EDITOR_RENDER_H */
