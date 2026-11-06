#include "arena.h"
#include "video.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int have_ffmpeg(void)
{
	return system("command -v ffmpeg >/dev/null 2>&1") == 0 &&
	       system("command -v ffprobe >/dev/null 2>&1") == 0;
}

int main(void)
{
	char path[300];
	char cmd[700];
	struct video_state vs;
	struct arena a;
	unsigned char *px;
	int w, h;
	int frame;
	int tries;

	if (!have_ffmpeg()) {
		printf("ffmpeg not found; video test skipped\n");
		return 0;
	}

	snprintf(path, sizeof(path), "/tmp/morph_video_%d.mp4",
		 (int)getpid());
	snprintf(cmd, sizeof(cmd),
		 "ffmpeg -hide_banner -loglevel error -f lavfi "
		 "-i testsrc=size=64x48:rate=10 -t 1 -pix_fmt yuv420p "
		 "-y '%s'", path);
	assert(system(cmd) == 0);

	arena_init(&a);
	assert(video_open(&a, path, &vs) == 0);
	assert(vs.width == 64 && vs.height == 48);
	assert(vs.total_frames > 0);

	px = video_get_frame(&a, &vs, 0, &w, &h);
	assert(px != NULL && w == 64 && h == 48);
	px = video_get_frame(&a, &vs, 0, &w, &h);
	assert(px != NULL && w == 64 && h == 48);

	video_request_frame(&vs, 3);
	frame = -1;
	px = NULL;
	for (tries = 0; tries < 200; tries++) {
		if (video_take_loaded(&vs, &frame, &px, &w, &h))
			break;
		usleep(20000);
	}
	assert(frame == 3);
	assert(px != NULL && w == 64 && h == 48);
	free(px);

	video_close(&vs);
	arena_free(&a);
	remove(path);
	printf("all video tests passed!\n");
	return 0;
}
