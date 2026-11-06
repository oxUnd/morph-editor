#include "headless.h"
#include "image.h"
#include "json_util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_prefix[256];

static void make_image(const char *path, int w, int h, int rx, int ry,
		       int rw, int rh)
{
	unsigned char *pixels;
	int x, y;

	pixels = malloc((size_t)w * h * 4);
	assert(pixels);
	memset(pixels, 0, (size_t)w * h * 4);
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			unsigned char *p = pixels + ((size_t)y * w + x) * 4;

			p[3] = 255;
			if (x >= rx && x < rx + rw &&
			    y >= ry && y < ry + rh) {
				p[0] = 255;
				p[1] = 0;
				p[2] = 0;
			}
		}
	}
	assert(image_save(path, pixels, w, h, 4) == 0);
	free(pixels);
}

static void test_crop_bounds(void)
{
	struct arena a;
	struct headless_args args;
	struct image out;
	char src[300], dst[300];

	arena_init(&a);
	snprintf(src, sizeof(src), "%s_src.png", g_prefix);
	make_image(src, 100, 80, -1, -1, 0, 0);

	memset(&args, 0, sizeof(args));
	args.path = src;
	snprintf(dst, sizeof(dst), "%s_crop1.png", g_prefix);
	args.output = dst;
	args.bbox_str = "-10,-10,20,20";
	assert(headless_crop(&a, src, &args) == 0);
	assert(image_load(&a, dst, &out) == 0);
	assert(out.width == 10 && out.height == 10);

	snprintf(dst, sizeof(dst), "%s_crop2.png", g_prefix);
	args.output = dst;
	args.bbox_str = "90,70,50,50";
	assert(headless_crop(&a, src, &args) == 0);
	assert(image_load(&a, dst, &out) == 0);
	assert(out.width == 10 && out.height == 10);

	args.bbox_str = "500,500,10,10";
	assert(headless_crop(&a, src, &args) < 0);

	/* Negative width is a right-to-left drag; it is normalized. */
	args.bbox_str = "10,10,-20,20";
	assert(headless_crop(&a, src, &args) == 0);
	assert(image_load(&a, dst, &out) == 0);
	assert(out.width == 10 && out.height == 20);

	arena_free(&a);
}

static void test_composite(void)
{
	struct arena a;
	struct headless_args args;
	struct image out;
	char src[300], dst[300];

	arena_init(&a);
	snprintf(src, sizeof(src), "%s_fg.png", g_prefix);
	make_image(src, 20, 20, 0, 0, 20, 20);

	memset(&args, 0, sizeof(args));
	args.composite_str = "30x20";
	args.placements[0].path = src;
	args.placements[0].x = 5;
	args.placements[0].y = 5;
	args.placement_count = 1;
	snprintf(dst, sizeof(dst), "%s_comp.png", g_prefix);
	args.output = dst;

	assert(headless_composite(&a, &args) == 0);
	assert(image_load(&a, dst, &out) == 0);
	assert(out.width == 30 && out.height == 20);
	assert(out.pixels[(5 * 30 + 5) * 4] == 255);

	arena_free(&a);
}

static char *read_file(const char *path)
{
	FILE *f;
	long sz;
	char *buf;

	f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc((size_t)sz + 1);
	if (buf) {
		if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
			free(buf);
			buf = NULL;
		} else {
			buf[sz] = '\0';
		}
	}
	fclose(f);
	return buf;
}

static void test_diff_regions(void)
{
	struct arena a;
	struct headless_args args;
	struct image out;
	char src[300], mod[300], dst[300], json_path[300];
	char *text;
	cJSON *root;
	cJSON *regions;
	cJSON *first;
	int saved;
	FILE *fp;

	arena_init(&a);
	snprintf(src, sizeof(src), "%s_diff_a.png", g_prefix);
	make_image(src, 40, 40, -1, -1, 0, 0);
	snprintf(mod, sizeof(mod), "%s_diff_b.png", g_prefix);
	make_image(mod, 40, 40, 10, 10, 10, 10);
	snprintf(dst, sizeof(dst), "%s_diff.png", g_prefix);
	snprintf(json_path, sizeof(json_path), "%s_diff.json", g_prefix);

	fflush(stdout);
	saved = dup(STDOUT_FILENO);
	assert(saved >= 0);
	fp = fopen(json_path, "w");
	assert(fp);
	dup2(fileno(fp), STDOUT_FILENO);
	fclose(fp);

	memset(&args, 0, sizeof(args));
	args.path = src;
	args.diff_path = mod;
	args.output = dst;
	assert(headless_diff(&a, src, mod, &args) == 0);
	fflush(stdout);
	dup2(saved, STDOUT_FILENO);
	close(saved);

	text = read_file(json_path);
	assert(text != NULL);
	root = cJSON_Parse(text);
	assert(root != NULL);
	assert(cJSON_GetObjectItem(root, "diff_pixels")->valueint == 100);
	regions = cJSON_GetObjectItem(root, "diff_bboxes");
	assert(regions != NULL);
	assert(cJSON_GetArraySize(regions) >= 1);
	first = cJSON_GetArrayItem(regions, 0);
	assert(cJSON_GetObjectItem(first, "x")->valueint <= 10);
	assert(cJSON_GetObjectItem(first, "w")->valueint >= 10);
	cJSON_Delete(root);
	free(text);

	assert(image_load(&a, dst, &out) == 0);
	assert(out.width == 40 && out.height == 40);
	remove(dst);
	remove(json_path);
	arena_free(&a);
}

int main(void)
{
	printf("headless tests:\n");
	snprintf(g_prefix, sizeof(g_prefix), "/tmp/morph_headless_%d",
		 (int)getpid());
	test_crop_bounds();
	test_composite();
	test_diff_regions();
	printf("all headless tests passed!\n");
	return 0;
}
