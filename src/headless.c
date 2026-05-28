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
		id = bbox_add(&bm, x, y, w, h,
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
		bbox_add(&bm, x, y, w, h, "", 0);
	}

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
		if (b64)
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

	cropped = arena_alloc(a, (size_t)w * h * img.channels);
	if (!cropped)
		return -1;

	{
		int row;
		int cw;

		cw = MIN(w, img.width - x);
		for (row = 0; row < h && row + y < img.height; row++) {
			memcpy(cropped + row * cw * img.channels,
			       img.pixels + ((row + y) * img.width + x) *
			       img.channels,
			       cw * img.channels);
		}
	}

	if (args->output) {
		image_save(args->output, cropped, w, h, img.channels);
	}

	if (args->output_base64) {
		char *b64;

		b64 = base64_encode(a, cropped, w * h * img.channels);
		if (b64)
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
		image_save(args->output, img.pixels, img.width,
			   img.height, img.channels);
	}

	if (args->output_base64) {
		char *b64;

		b64 = base64_encode(a, img.pixels,
				    img.width * img.height * img.channels);
		if (b64)
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

	if (args->transform_str) {
		/* Parse e.g. "resize:800x600" or "rotate:90" or "flip:h" */
		if (strncmp(args->transform_str, "resize:",
			    7) == 0) {
			sscanf(args->transform_str + 7, "%dx%d",
			       &opts.resize_w, &opts.resize_h);
		} else if (strncmp(args->transform_str, "rotate:",
				   7) == 0) {
			opts.rotate = atoi(args->transform_str + 7);
		} else if (strncmp(args->transform_str, "flip:",
				   5) == 0) {
			if (args->transform_str[5] == 'h')
				opts.flip_h = 1;
			else if (args->transform_str[5] == 'v')
				opts.flip_v = 1;
		}
	}

	transform_apply(a, &img.pixels, &img.width, &img.height,
			img.channels, opts);

	if (args->output) {
		image_save(args->output, img.pixels, img.width,
			   img.height, img.channels);
	}

	if (args->output_base64) {
		char *b64;

		b64 = base64_encode(a, img.pixels,
				    img.width * img.height * img.channels);
		if (b64)
			printf("%s\n", b64);
	}

	return 0;
}

int headless_composite(struct arena *a, struct headless_args *args)
{
	/* Phase 2: full composite support */
	(void)a;
	(void)args;
	fprintf(stderr, "composite: not yet implemented\n");
	return -1;
}

int headless_convert(struct arena *a, const char *path,
		     struct headless_args *args)
{
	struct image img;

	if (image_load(a, path, &img) < 0)
		return -1;

	if (args->output) {
		image_save(args->output, img.pixels, img.width,
			   img.height, img.channels);
	}

	if (args->output_base64) {
		char *b64;

		b64 = base64_encode(a, img.pixels,
				    img.width * img.height * img.channels);
		if (b64)
			printf("%s\n", b64);
	}

	return 0;
}

int headless_diff(struct arena *a, const char *path1, const char *path2,
		  int output_base64)
{
	struct image img1, img2;
	int min_w, min_h;
	int diff_count;
	int total;
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
	diff_count = 0;
	total = min_w * min_h;

	{
		int i;
		int dr, dg, db;

		for (i = 0; i < total; i++) {
			dr = abs(img1.pixels[i * 4] - img2.pixels[i * 4]);
			dg = abs(img1.pixels[i * 4 + 1] -
				 img2.pixels[i * 4 + 1]);
			db = abs(img1.pixels[i * 4 + 2] -
				 img2.pixels[i * 4 + 2]);
			if (dr > 10 || dg > 10 || db > 10)
				diff_count++;
		}
	}

	similarity = total > 0 ? 1.0 - (double)diff_count / total : 1.0;

	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "similarity", similarity);

	diff_bboxes = cJSON_CreateArray();
	/* Phase 3: compute actual diff bboxes */
	cJSON_AddItemToObject(root, "diff_bboxes", diff_bboxes);

	result = cJSON_PrintUnformatted(root);
	printf("%s\n", result);
	free(result);
	cJSON_Delete(root);

	(void)output_base64;
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
		return headless_diff(a, args->path, args->diff_path,
				     args->output_base64);

	fprintf(stderr, "unknown command: %s\n", command);
	return -1;
}
