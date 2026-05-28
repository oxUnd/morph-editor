#include "video.h"
#include "image.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static int run_ffprobe(const char *path, double *fps, int *total_frames)
{
	char cmd[1024];
	FILE *fp;
	char buf[256];
	int ret;

	ret = snprintf(cmd, sizeof(cmd),
		       "ffprobe -v error -select_streams v:0 "
		       "-show_entries stream=r_frame_rate,nb_frames "
		       "-of csv=p=0 \"%s\" 2>/dev/null",
		       path);
	if (ret < 0 || ret >= (int)sizeof(cmd))
		return -1;

	fp = popen(cmd, "r");
	if (!fp)
		return -1;

	*fps = 0;
	*total_frames = 0;
	if (fgets(buf, sizeof(buf), fp)) {
		int num, den;

		if (sscanf(buf, "%d/%d,%d", &num, &den,
			   total_frames) == 3) {
			if (den > 0)
				*fps = (double)num / den;
		} else if (sscanf(buf, "%d/%d", &num, &den) == 2) {
			if (den > 0)
				*fps = (double)num / den;
		}
	}
	pclose(fp);

	if (*fps <= 0)
		*fps = 30.0;
	return 0;
}

int video_open(struct arena *a, const char *path, struct video_state *vs)
{
	memset(vs, 0, sizeof(*vs));

	vs->path = str_copy(a, path, strlen(path));
	if (!vs->path)
		return -1;

	if (run_ffprobe(path, &vs->fps, &vs->total_frames) < 0)
		return -1;

	vs->current_frame = 0;
	vs->cache_count = 0;
	return 0;
}

void video_close(struct video_state *vs)
{
	int i;

	if (vs->ffmpeg_running) {
		uv_process_kill(&vs->ffmpeg_proc, SIGTERM);
		vs->ffmpeg_running = 0;
	}
	for (i = 0; i < vs->cache_count; i++)
		free(vs->frame_cache[i]);
	free(vs->frame_buf);
	vs->frame_buf = NULL;
}

unsigned char *video_get_frame(struct arena *a, struct video_state *vs,
			       int frame_num, int *w, int *h)
{
	char cmd[1024];
	FILE *fp;
	unsigned char *png_data;
	int png_cap, png_len;
	int ret;
	struct image img;

	/* Check LRU cache */
	{
		int i;

		for (i = 0; i < vs->cache_count; i++) {
			if (vs->cache_frame_ids[i] == frame_num) {
				/* Cache stores PNG data */
				if (image_load_from_bytes(
						a,
						vs->frame_cache[i],
						0, /* need len */
						&img) == 0) {
					*w = img.width;
					*h = img.height;
					return img.pixels;
				}
			}
		}
	}

	ret = snprintf(cmd, sizeof(cmd),
		       "ffmpeg -i \"%s\" -vf \"select=eq(n\\,%d)\" "
		       "-vframes 1 -f image2pipe -vcodec png - "
		       "2>/dev/null",
		       vs->path, frame_num);
	if (ret < 0 || ret >= (int)sizeof(cmd))
		return NULL;

	fp = popen(cmd, "r");
	if (!fp)
		return NULL;

	png_cap = 1024 * 1024;
	png_data = malloc(png_cap);
	if (!png_data) {
		pclose(fp);
		return NULL;
	}

	png_len = 0;
	{
		size_t n;

		while ((n = fread(png_data + png_len, 1,
				  png_cap - png_len, fp)) > 0) {
			png_len += (int)n;
			if (png_len >= png_cap) {
				png_cap *= 2;
				{
					unsigned char *new_data;

					new_data = realloc(png_data,
							   png_cap);
					if (!new_data) {
						free(png_data);
						pclose(fp);
						return NULL;
					}
					png_data = new_data;
				}
			}
		}
	}
	pclose(fp);

	if (png_len == 0) {
		free(png_data);
		return NULL;
	}

	ret = image_load_from_bytes(a, png_data, png_len, &img);
	free(png_data);
	if (ret < 0)
		return NULL;

	*w = img.width;
	*h = img.height;
	return img.pixels;
}

int video_extract_frame_async(struct video_state *vs, uv_loop_t *loop,
			      int frame_num)
{
	(void)vs;
	(void)loop;
	(void)frame_num;
	return -1;
}
