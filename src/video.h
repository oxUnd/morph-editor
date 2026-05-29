#ifndef MORPH_EDITOR_VIDEO_H
#define MORPH_EDITOR_VIDEO_H

#include <stdint.h>
#include "arena.h"
#include "uv.h"

#define VIDEO_CACHE_SIZE 32

struct video_state {
	char *path;
	int total_frames;
	double fps;
	int width;
	int height;
	int current_frame;
	/* Cache stores raw RGBA pixels (width*height*4 each). */
	unsigned char *frame_cache[VIDEO_CACHE_SIZE];
	int cache_frame_ids[VIDEO_CACHE_SIZE];
	int cache_count;
	int cache_next; /* round-robin slot for replacement */
	uv_process_t ffmpeg_proc;
	uv_pipe_t ffmpeg_stdout;
	uv_pipe_t ffmpeg_stdin;
	int ffmpeg_running;
	unsigned char *frame_buf;
	int frame_buf_len;
	int frame_buf_cap;
};

/*
 * video_open - open video and get metadata via ffprobe
 */
int video_open(struct arena *a, const char *path, struct video_state *vs);

/*
 * video_close - close video and free resources
 */
void video_close(struct video_state *vs);

/*
 * video_get_frame - extract a single frame synchronously
 * Returns arena-allocated pixel buffer, sets *w, *h.
 */
unsigned char *video_get_frame(struct arena *a, struct video_state *vs,
			       int frame_num, int *w, int *h);

/*
 * video_extract_frame_async - start async frame extraction
 */
int video_extract_frame_async(struct video_state *vs, uv_loop_t *loop,
			      int frame_num);

#endif /* MORPH_EDITOR_VIDEO_H */
