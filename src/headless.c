#include "headless.h"
#include "image.h"
#include "bbox.h"
#include "mask.h"
#include "draw.h"
#include "canvas.h"
#include "transform.h"
#include "render.h"
#include "json_util.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_bbox_str(const char *str, int *x, int *y, int *w, int *h)
{
	if (!str)
		return -1;
	return sscanf(str, "%d,%d,%d,%d", x, y, w, h) == 4 ? 0 : -1;
}

/*
 * normalize_and_validate_bbox - normalize a user supplied bbox and
 * clamp it to the image bounds.
 *
 * Negative width/height describe a rectangle dragged from the
 * bottom-right corner, so they are normalized first. Everything
 * outside the image is clipped; an empty intersection is an error.
 * This is the single trust boundary for every headless command that
 * accepts --bbox, which keeps pixel copies from ever reading outside
 * the source buffer.
 */
static int normalize_and_validate_bbox(int *x, int *y, int *w, int *h,
				       int img_w, int img_h)
{
	if (*w < 0) {
		*x += *w;
		*w = -*w;
	}
	if (*h < 0) {
		*y += *h;
		*h = -*h;
	}
	if (*w <= 0 || *h <= 0)
		return -1;
	if (img_w <= 0 || img_h <= 0)
		return -1;
	if (*x < 0) {
		*w += *x;
		*x = 0;
	}
	if (*y < 0) {
		*h += *y;
		*y = 0;
	}
	if (*w <= 0 || *h <= 0)
		return -1;
	if (*x >= img_w || *y >= img_h)
		return -1;
	if (*w > img_w - *x)
		*w = img_w - *x;
	if (*h > img_h - *y)
		*h = img_h - *y;
	if (*w <= 0 || *h <= 0)
		return -1;
	return 0;
}

static int require_output(const struct headless_args *args)
{
	if (!args->output && !args->output_base64) {
		fprintf(stderr,
			"error: no output requested (use --output or "
			"--output-base64)\n");
		return -1;
	}
	return 0;
}

int headless_info(const char *path, int output_base64)
{
	char *info;

	(void)output_base64;

	info = image_info_json(path);
	if (!info) {
		fprintf(stderr, "error: cannot read %s\n", path);
		return -1;
	}
	printf("%s\n", info);
	free(info);
	return 0;
}

int headless_annotate(struct arena *a, const char *path,
		      struct headless_args *args)
{
	struct image img;
	struct bbox_manager bm;
	int x, y, w, h;
	int id;
	cJSON *root;
	cJSON *bboxes;
	char *result;

	if (image_load(a, path, &img) < 0) {
		fprintf(stderr, "error: cannot load %s\n", path);
		return -1;
	}

	bbox_manager_init(&bm);

	if (args->bbox_str) {
		if (parse_bbox_str(args->bbox_str, &x, &y, &w, &h) < 0) {
			fprintf(stderr, "error: invalid bbox format\n");
			return -1;
		}
		if (normalize_and_validate_bbox(&x, &y, &w, &h,
						img.width, img.height) < 0) {
			fprintf(stderr,
				"error: bbox is outside the image\n");
			return -1;
		}
		id = bbox_add(&bm, x, y, w, h, 0,
			      args->label ? args->label : "", 0);
		if (id < 0) {
			fprintf(stderr, "error: cannot add bbox\n");
			return -1;
		}
	}

	/* Save annotated image if output specified */
	if (args->output) {
		render_bboxes_on_pixels_raw(img.pixels, img.width,
					    img.height, img.channels, &bm);
		if (image_save(args->output, img.pixels,
			       img.width, img.height, img.channels) < 0) {
			fprintf(stderr, "error: cannot save %s\n",
				args->output);
			return -1;
		}
	}

	/* Output JSON */
	root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "path", path);
	cJSON_AddNumberToObject(root, "width", img.width);
	cJSON_AddNumberToObject(root, "height", img.height);

	bboxes = cJSON_CreateArray();
	{
		int i;

		for (i = 0; i < bm.count; i++)
			cJSON_AddItemToArray(bboxes,
					     bbox_to_json(&bm.boxes[i]));
	}
	cJSON_AddItemToObject(root, "bboxes", bboxes);

	result = cJSON_PrintUnformatted(root);
	printf("%s\n", result);
	free(result);
	cJSON_Delete(root);

	return 0;
}

int headless_mask(struct arena *a, const char *path,
		  struct headless_args *args)
{
	struct image img;
	struct bbox_manager bm;
	int x, y, w, h;
	struct mask_options opts;
	unsigned char *mask;

	if (image_load(a, path, &img) < 0) {
		fprintf(stderr, "error: cannot load %s\n", path);
		return -1;
	}

	bbox_manager_init(&bm);

	if (args->bbox_str) {
		if (parse_bbox_str(args->bbox_str, &x, &y, &w, &h) < 0) {
			fprintf(stderr, "error: invalid bbox format\n");
			return -1;
		}
		if (normalize_and_validate_bbox(&x, &y, &w, &h,
						img.width, img.height) < 0) {
			fprintf(stderr,
				"error: bbox is outside the image\n");
			return -1;
		}
		bbox_add(&bm, x, y, w, h, 0, "", 0);
	}

	if (require_output(args) < 0)
		return -1;

	opts.feather = args->feather_str ? atoi(args->feather_str) : 0;
	opts.invert = args->invert_mask;

	mask = mask_from_bboxes(a, bm.boxes, bm.count,
				img.width, img.height, opts);
	if (!mask) {
		fprintf(stderr, "error: cannot generate mask\n");
		return -1;
	}

	if (args->output) {
		if (image_save(args->output, mask, img.width, img.height, 1)
		    < 0) {
			fprintf(stderr, "error: cannot save mask\n");
			return -1;
		}
	}

	if (args->output_base64) {
		char *b64;

		b64 = base64_encode(a, mask, img.width * img.height);
		if (!b64) {
			fprintf(stderr, "error: base64 encode failed\n");
			return -1;
		}
		printf("%s\n", b64);
	}

	return 0;
}

int headless_crop(struct arena *a, const char *path,
		  struct headless_args *args)
{
	struct image img;
	int x, y, w, h;
	unsigned char *cropped;

	if (image_load(a, path, &img) < 0)
		return -1;

	if (parse_bbox_str(args->bbox_str, &x, &y, &w, &h) < 0)
		return -1;

	if (normalize_and_validate_bbox(&x, &y, &w, &h,
					img.width, img.height) < 0) {
		fprintf(stderr, "error: bbox is outside the image\n");
		return -1;
	}
	if (require_output(args) < 0)
		return -1;

	cropped = arena_alloc(a, (size_t)w * h * img.channels);
	if (!cropped)
		return -1;
	memset(cropped, 0, (size_t)w * h * img.channels);

	{
		int row;

		for (row = 0; row < h; row++) {
			memcpy(cropped + (size_t)row * w * img.channels,
			       img.pixels +
			       ((size_t)(row + y) * img.width + x) *
			       img.channels,
			       (size_t)w * img.channels);
		}
	}

	if (args->output) {
		if (image_save(args->output, cropped, w, h,
			       img.channels) < 0) {
			fprintf(stderr, "error: cannot save %s\n",
				args->output);
			return -1;
		}
	}

	if (args->output_base64) {
		char *b64;

		b64 = base64_encode(a, cropped, w * h * img.channels);
		if (!b64) {
			fprintf(stderr, "error: base64 encode failed\n");
			return -1;
		}
		printf("%s\n", b64);
	}

	return 0;
}

int headless_draw(struct arena *a, const char *path,
		  struct headless_args *args)
{
	struct image img;
	struct draw_command cmd;

	if (image_load(a, path, &img) < 0)
		return -1;

	memset(&cmd, 0, sizeof(cmd));

	if (require_output(args) < 0)
		return -1;

	if (args->draw_type) {
		if (strcmp(args->draw_type, "rect") == 0)
			cmd.type = DRAW_RECT;
		else if (strcmp(args->draw_type, "filled_rect") == 0)
			cmd.type = DRAW_FILLED_RECT;
		else if (strcmp(args->draw_type, "circle") == 0)
			cmd.type = DRAW_CIRCLE;
		else if (strcmp(args->draw_type, "filled_circle") == 0)
			cmd.type = DRAW_FILLED_CIRCLE;
		else if (strcmp(args->draw_type, "line") == 0)
			cmd.type = DRAW_LINE;
		else if (strcmp(args->draw_type, "arrow") == 0)
			cmd.type = DRAW_ARROW;
		else if (strcmp(args->draw_type, "text") == 0)
			cmd.type = DRAW_TEXT;
		else if (strcmp(args->draw_type, "fill") == 0)
			cmd.type = DRAW_FILL;
		else {
			fprintf(stderr, "error: unknown draw primitive\n");
			return -1;
		}
	}

	if (args->draw_params) {
		int x, y, w, h;

		if (sscanf(args->draw_params, "%d,%d,%d,%d",
			   &x, &y, &w, &h) == 4) {
			cmd.x = x;
			cmd.y = y;
			cmd.w = w;
			cmd.h = h;
		}
	}

	cmd.color = 0x00ff00;
	cmd.thickness = 2;

	draw_execute(img.pixels, img.width, img.height, img.channels, &cmd);

	if (args->output) {
		if (image_save(args->output, img.pixels, img.width,
			       img.height, img.channels) < 0) {
			fprintf(stderr, "error: cannot save %s\n",
				args->output);
			return -1;
		}
	}

	if (args->output_base64) {
		char *b64;

		b64 = base64_encode(a, img.pixels,
				    img.width * img.height * img.channels);
		if (!b64) {
			fprintf(stderr, "error: base64 encode failed\n");
			return -1;
		}
		printf("%s\n", b64);
	}

	return 0;
}

int headless_transform(struct arena *a, const char *path,
		       struct headless_args *args)
{
	struct image img;
	struct transform_options opts;

	if (image_load(a, path, &img) < 0)
		return -1;

	memset(&opts, 0, sizeof(opts));

	if (require_output(args) < 0)
		return -1;

	if (args->transform_str) {
		/* Parse e.g. "resize:800x600" or "rotate:90" or "flip:h" */
		if (strncmp(args->transform_str, "resize:",
			    7) == 0) {
			if (sscanf(args->transform_str + 7, "%dx%d",
				   &opts.resize_w, &opts.resize_h) != 2 ||
			    opts.resize_w <= 0 || opts.resize_h <= 0) {
				fprintf(stderr,
					"error: invalid resize (WxH)\n");
				return -1;
			}
		} else if (strncmp(args->transform_str, "rotate:",
				   7) == 0) {
			opts.rotate = atoi(args->transform_str + 7);
			if (opts.rotate % 90 != 0) {
				fprintf(stderr,
					"error: rotate must be a multiple "
					"of 90\n");
				return -1;
			}
		} else if (strncmp(args->transform_str, "flip:",
				   5) == 0) {
			if (args->transform_str[5] == 'h')
				opts.flip_h = 1;
			else if (args->transform_str[5] == 'v')
				opts.flip_v = 1;
			else {
				fprintf(stderr,
					"error: flip must be h or v\n");
				return -1;
			}
		}
	}

	transform_apply(a, &img.pixels, &img.width, &img.height,
			img.channels, opts);

	if (args->output) {
		if (image_save(args->output, img.pixels, img.width,
			       img.height, img.channels) < 0) {
			fprintf(stderr, "error: cannot save %s\n",
				args->output);
			return -1;
		}
	}

	if (args->output_base64) {
		char *b64;

		b64 = base64_encode(a, img.pixels,
				    img.width * img.height * img.channels);
		if (!b64) {
			fprintf(stderr, "error: base64 encode failed\n");
			return -1;
		}
		printf("%s\n", b64);
	}

	return 0;
}

int headless_composite(struct arena *a, struct headless_args *args)
{
	struct canvas_layer layers[MAX_PLACEMENTS];
	unsigned char *pixels;
	int cw, ch;
	int i;

	if (!args->composite_str ||
	    sscanf(args->composite_str, "%dx%d", &cw, &ch) != 2 ||
	    cw <= 0 || ch <= 0) {
		fprintf(stderr, "error: --canvas must be WxH\n");
		return -1;
	}
	if (args->placement_count <= 0) {
		fprintf(stderr, "error: no --place/--at given\n");
		return -1;
	}
	if (require_output(args) < 0)
		return -1;

	memset(layers, 0, sizeof(layers));
	for (i = 0; i < args->placement_count; i++) {
		layers[i].image_path = args->placements[i].path;
		layers[i].x = args->placements[i].x;
		layers[i].y = args->placements[i].y;
		layers[i].opacity = 1.0f;
		layers[i].blend = BLEND_NORMAL;
	}

	pixels = canvas_composite(a, cw, ch, layers,
				  args->placement_count, 0x000000);
	if (!pixels) {
		fprintf(stderr, "error: cannot composite\n");
		return -1;
	}

	if (args->output &&
	    image_save(args->output, pixels, cw, ch, 4) < 0) {
		fprintf(stderr, "error: cannot save %s\n", args->output);
		return -1;
	}

	if (args->output_base64) {
		char *b64;

		b64 = base64_encode(a, pixels, (size_t)cw * ch * 4);
		if (!b64) {
			fprintf(stderr, "error: base64 encode failed\n");
			return -1;
		}
		printf("%s\n", b64);
	}

	return 0;
}

int headless_convert(struct arena *a, const char *path,
		     struct headless_args *args)
{
	struct image img;

	if (image_load(a, path, &img) < 0)
		return -1;

	if (require_output(args) < 0)
		return -1;

	if (args->output) {
		if (image_save(args->output, img.pixels, img.width,
			       img.height, img.channels) < 0) {
			fprintf(stderr, "error: cannot save %s\n",
				args->output);
			return -1;
		}
	}

	if (args->output_base64) {
		char *b64;

		b64 = base64_encode(a, img.pixels,
				    img.width * img.height * img.channels);
		if (!b64) {
			fprintf(stderr, "error: base64 encode failed\n");
			return -1;
		}
		printf("%s\n", b64);
	}

	return 0;
}

#define DIFF_BLOCK_SIZE 8

static int diff_pixel_changed(const unsigned char *p1,
			      const unsigned char *p2)
{
	int dr, dg, db;

	dr = abs((int)p1[0] - (int)p2[0]);
	dg = abs((int)p1[1] - (int)p2[1]);
	db = abs((int)p1[2] - (int)p2[2]);
	return dr > 10 || dg > 10 || db > 10;
}

static void diff_add_region(cJSON *arr, int x, int y, int w, int h,
			    int min_w, int min_h)
{
	cJSON *o;

	if (x >= min_w || y >= min_h)
		return;
	if (x + w > min_w)
		w = min_w - x;
	if (y + h > min_h)
		h = min_h - y;
	if (w <= 0 || h <= 0)
		return;

	o = cJSON_CreateObject();
	if (!o)
		return;
	cJSON_AddNumberToObject(o, "x", x);
	cJSON_AddNumberToObject(o, "y", y);
	cJSON_AddNumberToObject(o, "w", w);
	cJSON_AddNumberToObject(o, "h", h);
	cJSON_AddItemToArray(arr, o);
}

/*
 * diff_find_regions - connected components over a block mask.
 *
 * The mask is 1 for a changed 8x8 block and 0 otherwise. Walking
 * blocks instead of pixels keeps memory bounded for large images
 * while still producing usable bounding boxes.
 */
static void diff_find_regions(struct arena *a, unsigned char *mask,
			      int bw, int bh, int min_w, int min_h,
			      cJSON *out)
{
	int *queue;
	int total;
	int i;

	total = bw * bh;
	queue = arena_alloc(a, (size_t)total * sizeof(int));
	if (!queue)
		return;

	for (i = 0; i < total; i++) {
		int minx, maxx, miny, maxy;
		int qh, qt, cur, cx, cy;

		if (mask[i] != 1)
			continue;

		minx = maxx = i % bw;
		miny = maxy = i / bw;
		mask[i] = 2;
		qh = 0;
		qt = 0;
		queue[qt++] = i;

		while (qh < qt) {
			cur = queue[qh++];
			cx = cur % bw;
			cy = cur / bw;

#define DIFF_VISIT(nx, ny) do {					\
	int ni = (ny) * bw + (nx);				\
	if ((nx) >= 0 && (nx) < bw && (ny) >= 0 &&		\
	    (ny) < bh && mask[ni] == 1) {			\
		mask[ni] = 2;					\
		queue[qt++] = ni;				\
		if ((nx) < minx) minx = (nx);			\
		if ((nx) > maxx) maxx = (nx);			\
		if ((ny) < miny) miny = (ny);			\
		if ((ny) > maxy) maxy = (ny);			\
	}							\
} while (0)

			DIFF_VISIT(cx - 1, cy);
			DIFF_VISIT(cx + 1, cy);
			DIFF_VISIT(cx, cy - 1);
			DIFF_VISIT(cx, cy + 1);
#undef DIFF_VISIT
		}

		diff_add_region(out, minx * DIFF_BLOCK_SIZE,
				miny * DIFF_BLOCK_SIZE,
				(maxx - minx + 1) * DIFF_BLOCK_SIZE,
				(maxy - miny + 1) * DIFF_BLOCK_SIZE,
				min_w, min_h);
	}
}

static unsigned char *diff_render_visual(struct arena *a,
					 const unsigned char *p1,
					 const unsigned char *p2,
					 int w, int h)
{
	unsigned char *vis;
	int i;

	vis = arena_alloc(a, (size_t)w * h * 4);
	if (!vis)
		return NULL;

	for (i = 0; i < w * h; i++) {
		unsigned char *d = vis + i * 4;

		if (diff_pixel_changed(p1 + i * 4, p2 + i * 4)) {
			d[0] = 255;
			d[1] = 0;
			d[2] = 0;
		} else {
			unsigned int g;

			g = (p1[i * 4] + p1[i * 4 + 1] +
			     p1[i * 4 + 2]) / 3;
			g = g / 3;
			d[0] = (unsigned char)g;
			d[1] = (unsigned char)g;
			d[2] = (unsigned char)g;
		}
		d[3] = 255;
	}
	return vis;
}

int headless_diff(struct arena *a, const char *path1, const char *path2,
		  struct headless_args *args)
{
	struct image img1, img2;
	unsigned char *blocks;
	unsigned char *vis;
	int min_w, min_h;
	int bw, bh;
	int diff_count;
	int total;
	int x, y;
	double similarity;
	cJSON *root;
	cJSON *diff_bboxes;
	char *result;

	if (image_load(a, path1, &img1) < 0)
		return -1;
	if (image_load(a, path2, &img2) < 0)
		return -1;

	min_w = MIN(img1.width, img2.width);
	min_h = MIN(img1.height, img2.height);
	if (min_w <= 0 || min_h <= 0) {
		fprintf(stderr, "error: empty image intersection\n");
		return -1;
	}

	bw = (min_w + DIFF_BLOCK_SIZE - 1) / DIFF_BLOCK_SIZE;
	bh = (min_h + DIFF_BLOCK_SIZE - 1) / DIFF_BLOCK_SIZE;
	blocks = arena_alloc(a, (size_t)bw * bh);
	if (!blocks)
		return -1;
	memset(blocks, 0, (size_t)bw * bh);

	diff_count = 0;
	total = min_w * min_h;
	for (y = 0; y < min_h; y++) {
		for (x = 0; x < min_w; x++) {
			int i = y * min_w + x;

			if (diff_pixel_changed(img1.pixels + i * 4,
					       img2.pixels + i * 4)) {
				diff_count++;
				blocks[(y / DIFF_BLOCK_SIZE) * bw +
				       x / DIFF_BLOCK_SIZE] = 1;
			}
		}
	}

	similarity = 1.0 - (double)diff_count / total;

	if (args->output_base64) {
		unsigned char *png;
		char *b64;
		int png_len;

		vis = diff_render_visual(a, img1.pixels, img2.pixels,
					 min_w, min_h);
		if (!vis)
			return -1;
		png = image_save_png_to_buf(vis, min_w, min_h, 4, &png_len);
		if (!png)
			return -1;
		b64 = base64_encode(a, png, png_len);
		free(png);
		if (!b64)
			return -1;
		printf("%s\n", b64);
		return 0;
	}

	root = cJSON_CreateObject();
	if (!root)
		return -1;
	cJSON_AddNumberToObject(root, "similarity", similarity);
	cJSON_AddNumberToObject(root, "diff_pixels", diff_count);
	cJSON_AddNumberToObject(root, "compared_pixels", total);
	cJSON_AddBoolToObject(root, "size_match",
			      img1.width == img2.width &&
			      img1.height == img2.height);

	diff_bboxes = cJSON_CreateArray();
	if (diff_bboxes) {
		diff_find_regions(a, blocks, bw, bh, min_w, min_h,
				  diff_bboxes);
		cJSON_AddItemToObject(root, "diff_bboxes", diff_bboxes);
	}

	result = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!result)
		return -1;
	printf("%s\n", result);
	free(result);

	if (args->output) {
		vis = diff_render_visual(a, img1.pixels, img2.pixels,
					 min_w, min_h);
		if (!vis ||
		    image_save(args->output, vis, min_w, min_h, 4) < 0) {
			fprintf(stderr, "error: cannot save %s\n",
				args->output);
			return -1;
		}
	}

	return 0;
}

int headless_run(struct arena *a, const char *command,
		 struct headless_args *args)
{
	if (strcmp(command, "info") == 0)
		return headless_info(args->path, args->output_base64);
	if (strcmp(command, "annotate") == 0)
		return headless_annotate(a, args->path, args);
	if (strcmp(command, "mask") == 0)
		return headless_mask(a, args->path, args);
	if (strcmp(command, "crop") == 0)
		return headless_crop(a, args->path, args);
	if (strcmp(command, "draw") == 0)
		return headless_draw(a, args->path, args);
	if (strcmp(command, "transform") == 0)
		return headless_transform(a, args->path, args);
	if (strcmp(command, "composite") == 0)
		return headless_composite(a, args);
	if (strcmp(command, "convert") == 0)
		return headless_convert(a, args->path, args);
	if (strcmp(command, "diff") == 0)
		return headless_diff(a, args->path, args->diff_path, args);

	fprintf(stderr, "unknown command: %s\n", command);
	return -1;
}
