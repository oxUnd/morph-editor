#include "video.h"
#include "image.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * run_process - fork/exec a command and capture its stdout.
 *
 * No shell is involved, so file names cannot inject commands.
 * stderr is discarded; short reads are reported through *got.
 */
static int run_process(char *const argv[], unsigned char *out, size_t cap,
		       size_t *got)
{
	int fds[2];
	pid_t pid;
	size_t total;
	int status;

	if (pipe(fds) < 0)
		return -1;
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if (pid == 0) {
		int devnull;

		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		close(fds[1]);
		devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execvp(argv[0], argv);
		_exit(127);
	}

	close(fds[1]);
	total = 0;
	while (total < cap) {
		ssize_t n;

		n = read(fds[0], out + total, cap - total);
		if (n <= 0)
			break;
		total += (size_t)n;
	}
	close(fds[0]);
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	*got = total;
	return 0;
}

static int run_ffprobe(const char *path, double *fps, int *total_frames,
		       int *width, int *height)
{
	char buf[512];
	char *argv[11];
	size_t got;
	int num, den, w, h, n;

	argv[0] = "ffprobe";
	argv[1] = "-v";
	argv[2] = "error";
	argv[3] = "-select_streams";
	argv[4] = "v:0";
	argv[5] = "-show_entries";
	argv[6] = "stream=r_frame_rate,nb_frames,width,height";
	argv[7] = "-of";
	argv[8] = "csv=p=0";
	argv[9] = (char *)path;
	argv[10] = NULL;

	memset(buf, 0, sizeof(buf));
	if (run_process(argv, (unsigned char *)buf, sizeof(buf) - 1,
			&got) < 0)
		return -1;
	buf[got] = '\0';

	*fps = 0;
	*total_frames = 0;
	*width = 0;
	*height = 0;
	if (sscanf(buf, "%d,%d,%d/%d,%d", &w, &h, &num, &den, &n) >= 4) {
		*width = w;
		*height = h;
		if (den > 0)
			*fps = (double)num / den;
		if (sscanf(strrchr(buf, ',') + 1, "%d", &n) == 1)
			*total_frames = n;
	}
	if (*fps <= 0)
		*fps = 30.0;
	return *width > 0 && *height > 0 ? 0 : -1;
}

static unsigned char *cache_lookup_locked(struct video_state *vs,
					  int frame_num)
{
	int i;

	for (i = 0; i < VIDEO_CACHE_SIZE; i++) {
		if (vs->cache_frame_ids[i] == frame_num &&
		    vs->frame_cache[i]) {
			vs->cache_stamp[i] = ++vs->cache_tick;
			return vs->frame_cache[i];
		}
	}
	return NULL;
}

static void cache_insert_locked(struct video_state *vs, int frame_num,
				unsigned char *rgba)
{
	int i;
	int slot;
	unsigned long oldest;

	slot = -1;
	for (i = 0; i < VIDEO_CACHE_SIZE; i++) {
		if (vs->cache_frame_ids[i] == frame_num) {
			free(vs->frame_cache[i]);
			vs->frame_cache[i] = rgba;
			vs->cache_stamp[i] = ++vs->cache_tick;
			return;
		}
		if (vs->cache_frame_ids[i] < 0 && slot < 0)
			slot = i;
	}

	if (slot < 0) {
		oldest = ~0UL;
		for (i = 0; i < VIDEO_CACHE_SIZE; i++) {
			if (vs->cache_stamp[i] < oldest) {
				oldest = vs->cache_stamp[i];
				slot = i;
			}
		}
		if (vs->frame_cache[slot])
			vs->cache_count--;
		free(vs->frame_cache[slot]);
	}

	vs->frame_cache[slot] = rgba;
	vs->cache_frame_ids[slot] = frame_num;
	vs->cache_stamp[slot] = ++vs->cache_tick;
	vs->cache_count++;
}

static unsigned char *video_extract_sync(struct video_state *vs,
					 int frame_num, int *w, int *h)
{
	char tbuf[64];
	char *argv[16];
	unsigned char *rgba;
	size_t expected;
	size_t got;
	double t;
	int hit;

	*w = vs->width;
	*h = vs->height;
	expected = (size_t)vs->width * vs->height * 4;

	pthread_mutex_lock(&vs->lock);
	{
		unsigned char *cached =
			cache_lookup_locked(vs, frame_num);

		if (cached) {
			rgba = malloc(expected);
			if (rgba)
				memcpy(rgba, cached, expected);
			pthread_mutex_unlock(&vs->lock);
			return rgba;
		}
	}
	pthread_mutex_unlock(&vs->lock);

	t = (vs->fps > 0) ? ((double)frame_num / vs->fps) : 0;
	snprintf(tbuf, sizeof(tbuf), "%.3f", t);

	argv[0] = "ffmpeg";
	argv[1] = "-hide_banner";
	argv[2] = "-loglevel";
	argv[3] = "error";
	argv[4] = "-ss";
	argv[5] = tbuf;
	argv[6] = "-i";
	argv[7] = vs->path;
	argv[8] = "-vframes";
	argv[9] = "1";
	argv[10] = "-f";
	argv[11] = "rawvideo";
	argv[12] = "-pix_fmt";
	argv[13] = "rgba";
	argv[14] = "-";
	argv[15] = NULL;

	rgba = malloc(expected);
	if (!rgba)
		return NULL;
	hit = run_process(argv, rgba, expected, &got);
	if (hit < 0 || got != expected) {
		free(rgba);
		return NULL;
	}
	return rgba;
}

static void *video_loader(void *arg)
{
	struct video_state *vs;

	vs = arg;
	for (;;) {
		int frame, w, h;
		unsigned char *pixels;
		unsigned char *copy;
		size_t sz;

		pthread_mutex_lock(&vs->lock);
		while (vs->loader_running && vs->request_frame < 0)
			pthread_cond_wait(&vs->cond, &vs->lock);
		if (!vs->loader_running) {
			pthread_mutex_unlock(&vs->lock);
			break;
		}
		frame = vs->request_frame;
		vs->request_frame = -1;
		pthread_mutex_unlock(&vs->lock);

		pixels = video_extract_sync(vs, frame, &w, &h);
		if (!pixels) {
			pthread_mutex_lock(&vs->lock);
			vs->load_failed = 1;
			pthread_mutex_unlock(&vs->lock);
			continue;
		}
		sz = (size_t)w * h * 4;
		copy = malloc(sz);
		if (copy)
			memcpy(copy, pixels, sz);
		pthread_mutex_lock(&vs->lock);
		cache_insert_locked(vs, frame, pixels);
		if (copy) {
			free(vs->loaded_pixels);
			vs->loaded_pixels = copy;
			vs->loaded_frame = frame;
			vs->load_failed = 0;
		}
		pthread_mutex_unlock(&vs->lock);
	}
	return NULL;
}

int video_open(struct arena *a, const char *path, struct video_state *vs)
{
	int i;

	(void)a;
	memset(vs, 0, sizeof(*vs));
	for (i = 0; i < VIDEO_CACHE_SIZE; i++)
		vs->cache_frame_ids[i] = -1;
	vs->request_frame = -1;
	vs->loaded_frame = -1;
	snprintf(vs->path, sizeof(vs->path), "%s", path);

	if (pthread_mutex_init(&vs->lock, NULL) != 0)
		return -1;
	if (pthread_cond_init(&vs->cond, NULL) != 0) {
		pthread_mutex_destroy(&vs->lock);
		return -1;
	}
	if (run_ffprobe(path, &vs->fps, &vs->total_frames,
			&vs->width, &vs->height) < 0) {
		pthread_cond_destroy(&vs->cond);
		pthread_mutex_destroy(&vs->lock);
		return -1;
	}

	vs->current_frame = 0;
	vs->loader_running = 1;
	if (pthread_create(&vs->loader, NULL, video_loader, vs) == 0)
		vs->loader_started = 1;
	else
		vs->loader_running = 0;
	return 0;
}

void video_close(struct video_state *vs)
{
	int i;

	if (vs->loader_started) {
		pthread_mutex_lock(&vs->lock);
		vs->loader_running = 0;
		pthread_cond_signal(&vs->cond);
		pthread_mutex_unlock(&vs->lock);
		pthread_join(vs->loader, NULL);
		vs->loader_started = 0;
	}
	for (i = 0; i < VIDEO_CACHE_SIZE; i++) {
		free(vs->frame_cache[i]);
		vs->frame_cache[i] = NULL;
		vs->cache_frame_ids[i] = -1;
	}
	vs->cache_count = 0;
	free(vs->loaded_pixels);
	vs->loaded_pixels = NULL;
	pthread_mutex_destroy(&vs->lock);
	pthread_cond_destroy(&vs->cond);
}

unsigned char *video_get_frame(struct arena *a, struct video_state *vs,
			       int frame_num, int *w, int *h)
{
	unsigned char *rgba;
	unsigned char *out;
	size_t sz;

	if (vs->width <= 0 || vs->height <= 0)
		return NULL;
	*w = vs->width;
	*h = vs->height;
	sz = (size_t)vs->width * vs->height * 4;

	pthread_mutex_lock(&vs->lock);
	{
		unsigned char *cached =
			cache_lookup_locked(vs, frame_num);

		if (cached) {
			out = arena_alloc(a, sz);
			if (out)
				memcpy(out, cached, sz);
			pthread_mutex_unlock(&vs->lock);
			return out;
		}
	}
	pthread_mutex_unlock(&vs->lock);

	rgba = video_extract_sync(vs, frame_num, w, h);
	if (!rgba)
		return NULL;
	out = arena_alloc(a, sz);
	if (out)
		memcpy(out, rgba, sz);
	pthread_mutex_lock(&vs->lock);
	cache_insert_locked(vs, frame_num, rgba);
	pthread_mutex_unlock(&vs->lock);
	return out;
}

void video_request_frame(struct video_state *vs, int frame_num)
{
	if (!vs->loader_started)
		return;
	pthread_mutex_lock(&vs->lock);
	if (vs->loaded_pixels && vs->loaded_frame == frame_num) {
		pthread_mutex_unlock(&vs->lock);
		return;
	}
	if (vs->request_frame != frame_num) {
		vs->request_frame = frame_num;
		pthread_cond_signal(&vs->cond);
	}
	pthread_mutex_unlock(&vs->lock);
}

int video_take_loaded(struct video_state *vs, int *frame_num,
		      unsigned char **pixels, int *w, int *h)
{
	int ret;

	ret = 0;
	pthread_mutex_lock(&vs->lock);
	if (vs->loaded_pixels) {
		*pixels = vs->loaded_pixels;
		vs->loaded_pixels = NULL;
		*frame_num = vs->loaded_frame;
		*w = vs->width;
		*h = vs->height;
		ret = 1;
	}
	pthread_mutex_unlock(&vs->lock);
	return ret;
}

int video_is_loading(struct video_state *vs)
{
	int ret;

	pthread_mutex_lock(&vs->lock);
	ret = vs->request_frame >= 0;
	pthread_mutex_unlock(&vs->lock);
	return ret;
}
