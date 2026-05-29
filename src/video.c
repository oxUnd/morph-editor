#include "video.h"
#include "image.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static int run_ffprobe(const char *path, double *fps, int *total_frames,
		       int *width, int *height)
{
	char cmd[1024];
	FILE *fp;
	char buf[256];
	int ret;

	ret = snprintf(cmd, sizeof(cmd),
		       "ffprobe -v error -select_streams v:0 "
		       "-show_entries stream=r_frame_rate,nb_frames,"
		       "width,height "
		       "-of csv=p=0 \"%s\" 2>/dev/null",
		       path);
	if (ret < 0 || ret >= (int)sizeof(cmd))
		return -1;

	fp = popen(cmd, "r");
	if (!fp)
		return -1;

	*fps = 0;
	*total_frames = 0;
	*width = 0;
	*height = 0;
	if (fgets(buf, sizeof(buf), fp)) {
		int num, den, w, h, n;

		/*
		 * ffprobe csv output ordering follows -show_entries
		 * argument order: width,height,r_frame_rate,nb_frames
		 */
		if (sscanf(buf, "%d,%d,%d/%d,%d",
			   &w, &h, &num, &den, &n) >= 4) {
			*width = w;
			*height = h;
			if (den > 0)
				*fps = (double)num / den;
			if (sscanf(strrchr(buf, ',') + 1, "%d", &n) == 1)
				*total_frames = n;
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

	if (run_ffprobe(path, &vs->fps, &vs->total_frames,
			&vs->width, &vs->height) < 0)
		return -1;

	vs->current_frame = 0;
	vs->cache_count = 0;
	vs->cache_next = 0;
	return 0;
}

void video_close(struct video_state *vs)
{
	int i;

	if (vs->ffmpeg_running) {
		uv_process_kill(&vs->ffmpeg_proc, SIGTERM);
		vs->ffmpeg_running = 0;
	}
	for (i = 0; i < vs->cache_count; i++) {
		free(vs->frame_cache[i]);
		vs->frame_cache[i] = NULL;
	}
	vs->cache_count = 0;
	free(vs->frame_buf);
	vs->frame_buf = NULL;
}

/*
 * cache_lookup - find frame in LRU cache, return raw rgba buffer or NULL.
 */
static unsigned char *cache_lookup(struct video_state *vs, int frame_num)
{
	int i;

	for (i = 0; i < vs->cache_count; i++) {
		if (vs->cache_frame_ids[i] == frame_num)
			return vs->frame_cache[i];
	}
	return NULL;
}

/*
 * cache_insert - put a frame's raw rgba buffer into cache, takes ownership.
 */
static void cache_insert(struct video_state *vs, int frame_num,
			 unsigned char *rgba)
{
	int slot;

	if (vs->cache_count < VIDEO_CACHE_SIZE) {
		slot = vs->cache_count++;
	} else {
		slot = vs->cache_next;
		vs->cache_next = (vs->cache_next + 1) % VIDEO_CACHE_SIZE;
		free(vs->frame_cache[slot]);
	}
	vs->frame_cache[slot] = rgba;
	vs->cache_frame_ids[slot] = frame_num;
}

unsigned char *video_get_frame(struct arena *a, struct video_state *vs,
			       int frame_num, int *w, int *h)
{
	char cmd[1024];
	FILE *fp;
	unsigned char *rgba;
	size_t expected, got;
	double t;
	int ret;

	if (vs->width <= 0 || vs->height <= 0)
		return NULL;

	*w = vs->width;
	*h = vs->height;
	expected = (size_t)vs->width * vs->height * 4;

	/* LRU cache hit: copy into arena and return. */
	rgba = cache_lookup(vs, frame_num);
	if (rgba) {
		unsigned char *out = arena_alloc(a, expected);
		if (!out)
			return NULL;
		memcpy(out, rgba, expected);
		return out;
	}

	/*
	 * Use input-side -ss for fast seek (keyframe-accurate but
	 * O(1) instead of decoding from start), and rawvideo rgba
	 * output to skip PNG encode/decode roundtrip.
	 */
	t = (vs->fps > 0) ? ((double)frame_num / vs->fps) : 0;
	ret = snprintf(cmd, sizeof(cmd),
		       "ffmpeg -hide_banner -loglevel error "
		       "-ss %.3f -i \"%s\" -vframes 1 "
		       "-f rawvideo -pix_fmt rgba - "
		       "2>/dev/null",
		       t, vs->path);
	if (ret < 0 || ret >= (int)sizeof(cmd))
		return NULL;

	fp = popen(cmd, "r");
	if (!fp)
		return NULL;

	rgba = malloc(expected);
	if (!rgba) {
		pclose(fp);
		return NULL;
	}

	got = 0;
	{
		size_t n;

		while (got < expected) {
			n = fread(rgba + got, 1, expected - got, fp);
			if (n == 0)
				break;
			got += n;
		}
	}
	pclose(fp);

	if (got != expected) {
		free(rgba);
		return NULL;
	}

	{
		unsigned char *out = arena_alloc(a, expected);
		if (!out) {
			free(rgba);
			return NULL;
		}
		memcpy(out, rgba, expected);
		cache_insert(vs, frame_num, rgba);
		return out;
	}
}

int video_extract_frame_async(struct video_state *vs, uv_loop_t *loop,
			      int frame_num)
{
	(void)vs;
	(void)loop;
	(void)frame_num;
	return -1;
}
