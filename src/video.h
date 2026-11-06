#ifndef MORPH_EDITOR_VIDEO_H
#define MORPH_EDITOR_VIDEO_H

#include <stdint.h>
#include <pthread.h>
#include "arena.h"

#define VIDEO_CACHE_SIZE 32

struct video_state {
	char path[1024];
	int total_frames;
	double fps;
	int width;
	int height;
	int current_frame;

	unsigned char *frame_cache[VIDEO_CACHE_SIZE];
	int cache_frame_ids[VIDEO_CACHE_SIZE];
	unsigned long cache_stamp[VIDEO_CACHE_SIZE];
	int cache_count;
	unsigned long cache_tick;

	pthread_t loader;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	int loader_started;
	int loader_running;
	int request_frame;
	int loaded_frame;
	unsigned char *loaded_pixels;
	int load_failed;
};

/*
 * video_open - open video and get metadata via ffprobe.
 */
int video_open(struct arena *a, const char *path, struct video_state *vs);

/*
 * video_close - stop the loader thread and free frames.
 */
void video_close(struct video_state *vs);

/*
 * video_get_frame - synchronous single-frame extraction.
 * Returns an arena-allocated RGBA buffer. Used for the first frame
 * and as a fallback; interactive seeking uses the async path.
 */
unsigned char *video_get_frame(struct arena *a, struct video_state *vs,
			       int frame_num, int *w, int *h);

/*
 * video_request_frame - ask the loader thread for a frame.
 * Coalesces repeated requests; the newest one wins.
 */
void video_request_frame(struct video_state *vs, int frame_num);

/*
 * video_take_loaded - take ownership of a completed frame.
 * Returns 1 and sets the outputs when a frame is available,
 * 0 when nothing is ready. The caller must free(*pixels).
 */
int video_take_loaded(struct video_state *vs, int *frame_num,
		      unsigned char **pixels, int *w, int *h);

int video_is_loading(struct video_state *vs);

#endif /* MORPH_EDITOR_VIDEO_H */
