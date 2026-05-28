#ifndef MORPH_EDITOR_HEADLESS_H
#define MORPH_EDITOR_HEADLESS_H

#include "arena.h"
#include "bbox.h"

struct headless_args {
	char *path;
	char *output;
	int output_base64;
	int input_base64;
	char *bbox_str;
	char *label;
	char *feather_str;
	int invert_mask;
	char *draw_type;
	char *draw_params;
	char *transform_str;
	char *format;
	int crop;
	char *composite_str;
	char *diff_path;
};

/*
 * headless_run - execute headless (non-interactive) command
 * Returns 0 on success.
 */
int headless_run(struct arena *a, const char *command,
		 struct headless_args *args);

/*
 * headless_info - output image info as JSON
 */
int headless_info(const char *path, int output_base64);

/*
 * headless_annotate - add bbox annotation to image
 */
int headless_annotate(struct arena *a, const char *path,
		      struct headless_args *args);

/*
 * headless_mask - generate mask from bbox
 */
int headless_mask(struct arena *a, const char *path,
		  struct headless_args *args);

/*
 * headless_crop - crop image region
 */
int headless_crop(struct arena *a, const char *path,
		  struct headless_args *args);

/*
 * headless_draw - draw on image
 */
int headless_draw(struct arena *a, const char *path,
		  struct headless_args *args);

/*
 * headless_transform - apply transform to image
 */
int headless_transform(struct arena *a, const char *path,
		       struct headless_args *args);

/*
 * headless_composite - composite images
 */
int headless_composite(struct arena *a, struct headless_args *args);

/*
 * headless_convert - convert image format
 */
int headless_convert(struct arena *a, const char *path,
		     struct headless_args *args);

/*
 * headless_diff - compare two images
 */
int headless_diff(struct arena *a, const char *path1, const char *path2,
		  int output_base64);

#endif /* MORPH_EDITOR_HEADLESS_H */
