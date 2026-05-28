#include "render.h"
#include "json_util.h"
#include "util.h"

#include "termbox2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- coordinate conversion ---- */

void term_to_pixel(int tx, int ty, int *px, int *py,
		   int img_w, int img_h,
		   int offset_x, int offset_y, float scale)
{
	*px = (int)((tx - offset_x) / scale);
	*py = (int)((ty - offset_y) / scale * 2);
	if (*px < 0)
		*px = 0;
	if (*py < 0)
		*py = 0;
	if (*px >= img_w)
		*px = img_w - 1;
	if (*py >= img_h)
		*py = img_h - 1;
}

void pixel_to_term(int px, int py, int *tx, int *ty,
		   int offset_x, int offset_y, float scale)
{
	*tx = (int)(px * scale) + offset_x;
	*ty = (int)(py * scale / 2) + offset_y;
}

/* ---- render state ---- */

void render_init(struct render_state *rs, enum term_proto proto)
{
	memset(rs, 0, sizeof(*rs));
	rs->proto = proto;
	rs->kitty_placement_id = 1;
	rs->dirty = 1;
}

void render_calc_fit(struct render_state *rs, int img_w, int img_h,
		     int term_w, int term_h)
{
	float scale_x, scale_y;
	int avail_w;

	/* Reserve 3 rows for status bar */
	term_h -= 3;

	/*
	 * Reserve a right-hand sidebar that lists bbox info,
	 * but only when the terminal is wide enough that the
	 * remaining image area is still usable.
	 */
	avail_w = term_w;
	if (term_w >= 60) {
		avail_w = term_w - SIDEBAR_W;
		rs->sidebar_w = SIDEBAR_W;
	} else {
		rs->sidebar_w = 0;
	}

	/* Each terminal cell is 2 pixel rows tall */
	scale_x = (float)avail_w / img_w;
	scale_y = (float)(term_h * 2) / img_h;
	rs->scale = MIN(scale_x, scale_y);

	rs->img_display_w = (int)(img_w * rs->scale);
	rs->img_display_h = (int)(img_h * rs->scale);

	rs->offset_x = (avail_w - rs->img_display_w) / 2;
	rs->offset_y = 0;

	rs->term_w = term_w;
	rs->term_h = term_h + 2;
}

/* ---- pixel drawing helpers ---- */

static void set_pixel(unsigned char *pixels, int w, int h, int channels,
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

void draw_rect_outline(unsigned char *pixels, int w, int h, int channels,
		       int rx, int ry, int rw, int rh,
		       uint32_t color, int thickness)
{
	int i, t;

	for (t = 0; t < thickness; t++) {
		for (i = 0; i < rw; i++) {
			set_pixel(pixels, w, h, channels, rx + i, ry + t,
				  color);
			set_pixel(pixels, w, h, channels, rx + i,
				  ry + rh - 1 - t, color);
		}
		for (i = 0; i < rh; i++) {
			set_pixel(pixels, w, h, channels, rx + t, ry + i,
				  color);
			set_pixel(pixels, w, h, channels, rx + rw - 1 - t,
				  ry + i, color);
		}
	}
}

/* ---- bbox overlay rendering ---- */

void render_bboxes_on_pixels(unsigned char *pixels, int w, int h,
			     int channels, struct bbox_manager *bm,
			     float scale, int offset_x, int offset_y)
{
	int i;
	struct bbox *b;
	int px, py, pw, ph;
	int is_selected;
	int hsize;
	uint32_t hl;

	for (i = 0; i < bm->count; i++) {
		b = &bm->boxes[i];
		px = (int)(b->x * scale) + offset_x;
		py = (int)(b->y * scale / 2) + offset_y;
		pw = (int)(b->w * scale);
		ph = (int)(b->h * scale / 2);

		is_selected = (i == bm->selected);

		draw_rect_outline(pixels, w, h, channels,
				  px, py, pw, ph, b->color,
				  is_selected ? 4 : 1);

		if (is_selected) {
			/* Bright white highlight on top of bbox color */
			hl = 0xffffff;
			draw_rect_outline(pixels, w, h, channels,
					  px - 2, py - 2,
					  pw + 4, ph + 4, hl, 1);
			/* Corner handles */
			hsize = 4;
			draw_rect_outline(pixels, w, h, channels,
					  px - hsize / 2,
					  py - hsize / 2,
					  hsize, hsize, hl, 2);
			draw_rect_outline(pixels, w, h, channels,
					  px + pw - hsize / 2,
					  py - hsize / 2,
					  hsize, hsize, hl, 2);
			draw_rect_outline(pixels, w, h, channels,
					  px - hsize / 2,
					  py + ph - hsize / 2,
					  hsize, hsize, hl, 2);
			draw_rect_outline(pixels, w, h, channels,
					  px + pw - hsize / 2,
					  py + ph - hsize / 2,
					  hsize, hsize, hl, 2);
		}
	}
}

void render_bboxes_on_pixels_raw(unsigned char *pixels, int w, int h,
				 int channels, struct bbox_manager *bm)
{
	int i;
	struct bbox *b;
	int is_selected;
	int hsize;
	uint32_t hl;

	for (i = 0; i < bm->count; i++) {
		b = &bm->boxes[i];
		is_selected = (i == bm->selected);

		draw_rect_outline(pixels, w, h, channels,
				  b->x, b->y, b->w, b->h, b->color,
				  is_selected ? 4 : 1);

		if (is_selected) {
			hl = 0xffffff;
			/* Outer highlight ring */
			draw_rect_outline(pixels, w, h, channels,
					  b->x - 2, b->y - 2,
					  b->w + 4, b->h + 4, hl, 1);
			/* Corner handles */
			hsize = MAX(8, MIN(b->w, b->h) / 16);
			draw_rect_outline(pixels, w, h, channels,
					  b->x - hsize / 2,
					  b->y - hsize / 2,
					  hsize, hsize, hl, 2);
			draw_rect_outline(pixels, w, h, channels,
					  b->x + b->w - hsize / 2,
					  b->y - hsize / 2,
					  hsize, hsize, hl, 2);
			draw_rect_outline(pixels, w, h, channels,
					  b->x - hsize / 2,
					  b->y + b->h - hsize / 2,
					  hsize, hsize, hl, 2);
			draw_rect_outline(pixels, w, h, channels,
					  b->x + b->w - hsize / 2,
					  b->y + b->h - hsize / 2,
					  hsize, hsize, hl, 2);
		}
	}
}

/* ---- Sixel encoding (median cut quantization) ---- */

struct color_box {
	int r_min, r_max;
	int g_min, g_max;
	int b_min, b_max;
	int count;
	int *indices;
};

static void compute_box_bounds(const unsigned char *pixels, int channels,
			       struct color_box *box)
{
	int i, idx;
	unsigned char r, g, b;

	box->r_min = 255; box->r_max = 0;
	box->g_min = 255; box->g_max = 0;
	box->b_min = 255; box->b_max = 0;

	for (i = 0; i < box->count; i++) {
		idx = box->indices[i];
		r = pixels[idx * channels];
		g = pixels[idx * channels + 1];
		b = pixels[idx * channels + 2];
		if (r < box->r_min) box->r_min = r;
		if (r > box->r_max) box->r_max = r;
		if (g < box->g_min) box->g_min = g;
		if (g > box->g_max) box->g_max = g;
		if (b < box->b_min) box->b_min = b;
		if (b > box->b_max) box->b_max = b;
	}
}

static int box_range(struct color_box *box)
{
	int r_range, g_range, b_range;

	r_range = box->r_max - box->r_min;
	g_range = box->g_max - box->g_min;
	b_range = box->b_max - box->b_min;
	return MAX(r_range, MAX(g_range, b_range));
}

static void sort_by_channel(const unsigned char *pixels, int channels,
			    int *indices, int count, int channel)
{
	/* Simple insertion sort by channel value */
	int i, j, key;
	int ch_val;

	for (i = 1; i < count; i++) {
		key = indices[i];
		ch_val = pixels[key * channels + channel];
		j = i - 1;
		while (j >= 0 &&
		       pixels[indices[j] * channels + channel] > ch_val) {
			indices[j + 1] = indices[j];
			j--;
		}
		indices[j + 1] = key;
	}
}

#define SIXEL_MAX_COLORS 256

static int median_cut(const unsigned char *pixels, int n_pixels,
		      int channels, uint32_t *palette, int max_colors)
{
	struct color_box boxes[SIXEL_MAX_COLORS];
	int n_boxes;
	int split_idx;
	int half;
	int i, j;
	int *all_indices;
	uint32_t avg_r, avg_g, avg_b;
	struct color_box *box;
	int *left_idx, *right_idx;

	all_indices = malloc(n_pixels * sizeof(int));
	if (!all_indices)
		return -1;
	for (i = 0; i < n_pixels; i++)
		all_indices[i] = i;

	boxes[0].indices = all_indices;
	boxes[0].count = n_pixels;
	compute_box_bounds(pixels, channels, &boxes[0]);
	n_boxes = 1;

	while (n_boxes < max_colors) {
		/* Find box with largest range */
		split_idx = -1;
		{
			int best_range = 0;
			int range;

			for (i = 0; i < n_boxes; i++) {
				if (boxes[i].count < 2)
					continue;
				range = box_range(&boxes[i]);
				if (range > best_range) {
					best_range = range;
					split_idx = i;
				}
			}
		}
		if (split_idx < 0)
			break;

		box = &boxes[split_idx];
		{
			int r_range, g_range, b_range, ch;

			r_range = box->r_max - box->r_min;
			g_range = box->g_max - box->g_min;
			b_range = box->b_max - box->b_min;
			ch = 0;
			if (g_range >= r_range && g_range >= b_range)
				ch = 1;
			else if (b_range >= r_range && b_range >= g_range)
				ch = 2;

			sort_by_channel(pixels, channels, box->indices,
					box->count, ch);
		}

		half = box->count / 2;

		left_idx = malloc(half * sizeof(int));
		right_idx = malloc((box->count - half) * sizeof(int));
		if (!left_idx || !right_idx) {
			free(left_idx);
			free(right_idx);
			break;
		}
		memcpy(left_idx, box->indices, half * sizeof(int));
		memcpy(right_idx, box->indices + half,
		       (box->count - half) * sizeof(int));

		boxes[n_boxes].indices = right_idx;
		boxes[n_boxes].count = box->count - half;
		compute_box_bounds(pixels, channels, &boxes[n_boxes]);

		box->indices = left_idx;
		box->count = half;
		compute_box_bounds(pixels, channels, box);

		n_boxes++;
	}

	/* Compute average color for each box */
	for (i = 0; i < n_boxes; i++) {
		avg_r = avg_g = avg_b = 0;
		for (j = 0; j < boxes[i].count; j++) {
			int idx = boxes[i].indices[j];

			avg_r += pixels[idx * channels];
			avg_g += pixels[idx * channels + 1];
			avg_b += pixels[idx * channels + 2];
		}
		if (boxes[i].count > 0) {
			avg_r /= boxes[i].count;
			avg_g /= boxes[i].count;
			avg_b /= boxes[i].count;
		}
		palette[i] = COLOR_RGB(avg_r, avg_g, avg_b);
	}

	for (i = 0; i < n_boxes; i++)
		free(boxes[i].indices);
	return n_boxes;
}

static int find_nearest_color(const unsigned char *pixels, int channels,
			      int pixel_idx, const uint32_t *palette,
			      int n_colors)
{
	int i, best;
	int best_dist, dist;
	int dr, dg, db;
	unsigned char r, g, b;

	r = pixels[pixel_idx * channels];
	g = pixels[pixel_idx * channels + 1];
	b = pixels[pixel_idx * channels + 2];

	best = 0;
	best_dist = INT_MAX;
	for (i = 0; i < n_colors; i++) {
		dr = (int)r - (int)COLOR_R(palette[i]);
		dg = (int)g - (int)COLOR_G(palette[i]);
		db = (int)b - (int)COLOR_B(palette[i]);
		dist = dr * dr + dg * dg + db * db;
		if (dist < best_dist) {
			best_dist = dist;
			best = i;
		}
	}
	return best;
}

char *render_image_sixel(struct arena *a, const unsigned char *pixels,
			 int w, int h, int channels)
{
	uint32_t palette[SIXEL_MAX_COLORS];
	int n_colors;
	int *color_map;
	int n_pixels;
	int sixel_rows;
	int row, col, color;
	struct {
		char *data;
		int len;
		int cap;
	} buf;
	int r, c;

	n_pixels = w * h;

	n_colors = median_cut(pixels, n_pixels, channels, palette,
			      SIXEL_MAX_COLORS);
	if (n_colors <= 0)
		return NULL;

	color_map = malloc(n_pixels * sizeof(int));
	if (!color_map)
		return NULL;

	for (r = 0; r < n_pixels; r++)
		color_map[r] = find_nearest_color(pixels, channels, r,
						  palette, n_colors);

	sixel_rows = (h + 5) / 6;
	buf.cap = n_pixels * 2 + 4096;
	buf.data = arena_alloc(a, buf.cap);
	if (!buf.data) {
		free(color_map);
		return NULL;
	}
	buf.len = 0;

#define EMIT(ch) do { \
	if (buf.len >= buf.cap - 1) { \
		buf.cap *= 2; \
		/* Can't realloc arena, just truncate */ \
		goto done; \
	} \
	buf.data[buf.len++] = (ch); \
} while (0)

	/* DCS intro */
	EMIT('\033'); EMIT('P'); EMIT('q');

	/* Palette */
	for (color = 0; color < n_colors; color++) {
		char tmp[64];
		int len;

		len = snprintf(tmp, sizeof(tmp), "#%d;2;%d;%d;%d",
			       color,
			       (int)(COLOR_R(palette[color]) * 100 / 255),
			       (int)(COLOR_G(palette[color]) * 100 / 255),
			       (int)(COLOR_B(palette[color]) * 100 / 255));
		for (c = 0; c < len; c++)
			EMIT(tmp[c]);
	}

	/* Sixel data */
	for (row = 0; row < sixel_rows; row++) {
		int y_base = row * 6;

		for (color = 0; color < n_colors; color++) {
			int has_pixel = 0;

			for (col = 0; col < w; col++) {
				int bit;

				for (bit = 0; bit < 6; bit++) {
					int y = y_base + bit;

					if (y >= h)
						continue;
					if (color_map[y * w + col] == color) {
						has_pixel = 1;
						break;
					}
				}
				if (has_pixel)
					break;
			}
			if (!has_pixel)
				continue;

			/* Select color */
			{
				char tmp[16];
				int len;

				len = snprintf(tmp, sizeof(tmp),
					       "#%d", color);
				for (c = 0; c < len; c++)
					EMIT(tmp[c]);
			}

			/* Encode sixel data with RLE */
			{
				int prev_sixel = -1;
				int run = 0;

				for (col = 0; col < w; col++) {
					int sixel = 0;
					int bit;

					for (bit = 0; bit < 6; bit++) {
						int y = y_base + bit;

						if (y >= h)
							continue;
						if (color_map[y * w + col] ==
						    color)
							sixel |= (1 << bit);
					}

					if (sixel == prev_sixel) {
						run++;
					} else {
						if (run > 0) {
							if (run == 1) {
								EMIT(63 +
								     prev_sixel);
							} else {
								char rle[16];
								int rle_len;

								rle_len = snprintf(
									rle,
									sizeof(rle),
									"!%d",
									run);
								for (c = 0;
								     c < rle_len;
								     c++)
									EMIT(rle[c]);
								EMIT(63 +
								     prev_sixel);
							}
						}
						prev_sixel = sixel;
						run = 1;
					}
				}
				/* Flush last run */
				if (run > 0) {
					if (run == 1) {
						EMIT(63 + prev_sixel);
					} else {
						char rle[16];
						int rle_len;

						rle_len = snprintf(
							rle, sizeof(rle),
							"!%d", run);
						for (c = 0; c < rle_len;
						     c++)
							EMIT(rle[c]);
						EMIT(63 + prev_sixel);
					}
				}
			}
		}
		EMIT('-');
	}

	/* ST */
	EMIT('\033'); EMIT('\\');

#undef EMIT

done:
	buf.data[buf.len] = '\0';
	free(color_map);
	return buf.data;
}

/* ---- Kitty protocol encoding ---- */

char *render_image_kitty(struct arena *a, const unsigned char *pixels,
			 int w, int h, int channels, int placement_id,
			 int disp_cols, int disp_rows)
{
	unsigned char *png_data;
	int png_len;
	char *b64;
	int b64_len;
	char *result;
	int result_cap;
	int result_len;
	int chunk_size;
	int offset;
	char header[256];
	int header_len;

	png_data = image_save_png_to_buf(pixels, w, h, channels, &png_len);
	if (!png_data)
		return NULL;

	b64_len = (png_len * 4 + 2) / 3;
	b64 = arena_alloc(a, b64_len + 1);
	if (!b64) {
		free(png_data);
		return NULL;
	}

	{
		char *tmp;

		tmp = base64_encode(a, png_data, png_len);
		if (!tmp) {
			free(png_data);
			return NULL;
		}
		b64_len = strlen(tmp);
		memcpy(b64, tmp, b64_len);
		b64[b64_len] = '\0';
	}
	free(png_data);

	/* Build Kitty escape sequences with chunking */
	chunk_size = 4096;
	result_cap = b64_len + b64_len / chunk_size * 128 + 256;
	result = arena_alloc(a, result_cap);
	if (!result)
		return NULL;

	result_len = 0;
	offset = 0;

	while (offset < b64_len) {
		int remaining;
		int m;

		remaining = b64_len - offset;
		m = (remaining > chunk_size) ? 1 : 0;

		if (offset == 0) {
			/*
			 * z=-1 places the image *below* the text
			 * cell layer. Without it (default z=0),
			 * Kitty draws the image above text cells,
			 * which hides termbox-drawn overlays such
			 * as bbox outlines and the rubber-band
			 * rectangle.
			 *
			 * Side effect: at z=-1 the default-
			 * background cells sit on top of the
			 * image, which can make blank space look
			 * like it's covering the picture. The
			 * editor mitigates this by only writing
			 * cells that have actual overlay content
			 * (bbox edges, status bar, sidebar) so
			 * the rest of the image area shows
			 * through.
			 */
			header_len = snprintf(header, sizeof(header),
				"\033_Ga=T,f=100,s=%d,v=%d,c=%d,r=%d,z=-1,i=%d,q=2,m=%d;",
				w, h, disp_cols, disp_rows,
				placement_id, m);
		} else {
			header_len = snprintf(header, sizeof(header),
				"\033_Gq=2,m=%d;", m);
		}

		{
			int end_len;
			int chunk_len;

			chunk_len = MIN(remaining, chunk_size);
			end_len = 2; /* ESC \ */

			if (result_len + header_len + chunk_len + end_len + 1
			    > result_cap) {
				break;
			}
			memcpy(result + result_len, header, header_len);
			result_len += header_len;
			memcpy(result + result_len, b64 + offset,
			       chunk_len);
			result_len += chunk_len;
			result[result_len++] = '\033';
			result[result_len++] = '\\';
			offset += chunk_len;
		}
	}

	result[result_len] = '\0';
	return result;
}

/* ---- iTerm2 protocol encoding ---- */

char *render_image_iterm2(struct arena *a, const unsigned char *pixels,
			  int w, int h, int channels,
			  int disp_cols, int disp_rows)
{
	unsigned char *png_data;
	int png_len;
	char *b64;
	int b64_len;
	char header[256];
	int header_len;
	char *result;
	int result_len;

	png_data = image_save_png_to_buf(pixels, w, h, channels, &png_len);
	if (!png_data)
		return NULL;

	b64 = base64_encode(a, png_data, png_len);
	free(png_data);
	if (!b64)
		return NULL;

	b64_len = strlen(b64);

	header_len = snprintf(header, sizeof(header),
		"\033]1337;File=inline=1;width=%d;height=%d;preserveAspectRatio=0:",
		disp_cols, disp_rows);

	result_len = header_len + b64_len + 2; /* +2 for ST (\007) */
	result = arena_alloc(a, result_len + 1);
	if (!result)
		return NULL;

	memcpy(result, header, header_len);
	memcpy(result + header_len, b64, b64_len);
	result[header_len + b64_len] = '\007';
	result[header_len + b64_len + 1] = '\0';

	return result;
}

/* ---- terminal output helpers ---- */

void clear_screen(void)
{
	tb_send("\033[2J\033[H", 7);
}

void move_cursor(int row, int col)
{
	char buf[32];
	int len;

	len = snprintf(buf, sizeof(buf), "\033[%d;%dH", row, col);
	tb_send(buf, len);
}

void sixel_write(const char *data, int len)
{
	tb_send(data, len);
}

void kitty_write(const char *data, int len)
{
	tb_send(data, len);
}

void kitty_delete_placement(int placement_id)
{
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "\033_Ga=d,i=%d,q=2\033\\",
		       placement_id);
	tb_send(buf, len);
}

void render_status_bar(const char *filename, int img_w, int img_h,
		       struct bbox_manager *bm, const char *mode,
		       int undo_count, int term_w,
		       int img_rows)
{
	char line1[256];
	char line2[256];
	int len;
	int row;

	row = img_rows + 1;

	/* Separator */
	move_cursor(row, 1);
	{
		char sep[256];
		int i;

		for (i = 0; i < term_w && i < (int)sizeof(sep); i++)
			sep[i] = '-';
		len = MIN(term_w, (int)sizeof(sep));
		tb_send(sep, len);
	}

	/* Line 1: filename and dimensions */
	len = snprintf(line1, sizeof(line1),
		       " %s [%dx%d] | BBox: %d | Mode: %s | Undo: %d",
		       filename ? filename : "(none)", img_w, img_h,
		       bm->count, mode ? mode : "VIEW", undo_count);
	move_cursor(row + 1, 1);
	tb_send(line1, MIN(len, term_w));

	/* Line 2: shortcuts */
	len = snprintf(line2, sizeof(line2),
		       " [s]ave [d]elete [u]ndo [r]edo [tab] [q]uit");
	move_cursor(row + 2, 1);
	tb_send(line2, MIN(len, term_w));
}
