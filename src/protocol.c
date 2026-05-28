#include "protocol.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_stdin_alloc(uv_handle_t *handle, size_t suggested_size,
			   uv_buf_t *buf)
{
	struct protocol_state *ps;

	ps = handle->data;
	if (!ps)
		return;

	if (ps->buf_cap - ps->buf_len < (int)suggested_size) {
		ps->buf_cap = ps->buf_len + suggested_size + 4096;
		{
			char *new_buf;

			new_buf = realloc(ps->buf, ps->buf_cap);
			if (!new_buf)
				return;
			ps->buf = new_buf;
		}
	}
	buf->base = ps->buf + ps->buf_len;
	buf->len = ps->buf_cap - ps->buf_len;
}

static void on_stdin_read(uv_stream_t *stream, ssize_t nread,
			  const uv_buf_t *buf)
{
	struct protocol_state *ps;
	char *nl;
	cJSON *request;

	(void)buf;

	ps = stream->data;
	if (!ps)
		return;

	if (nread <= 0)
		return;

	ps->buf_len += nread;
	ps->buf[ps->buf_len] = '\0';

	while ((nl = strchr(ps->buf, '\n')) != NULL) {
		char *line;

		*nl = '\0';
		line = ps->buf;

		if (line[0]) {
			request = cJSON_Parse(line);
			if (request) {
				if (ps->on_request)
					ps->on_request(request,
						       ps->user_data);
				cJSON_Delete(request);
			}
		}

		ps->buf_len -= (nl - ps->buf + 1);
		memmove(ps->buf, nl + 1, ps->buf_len + 1);
	}
}

int protocol_init(struct arena *a, uv_loop_t *loop,
		  struct protocol_state *ps)
{
	(void)a;

	memset(ps, 0, sizeof(*ps));

	uv_pipe_init(loop, &ps->stdin_pipe, 0);
	uv_pipe_init(loop, &ps->stdout_pipe, 0);

	ps->buf_cap = 4096;
	ps->buf = malloc(ps->buf_cap);
	if (!ps->buf)
		return -1;
	ps->buf_len = 0;
	ps->buf[0] = '\0';

	ps->stdin_pipe.data = ps;
	return 0;
}

void protocol_start_read(struct protocol_state *ps)
{
	uv_pipe_open(&ps->stdin_pipe, 0);
	uv_read_start((uv_stream_t *)&ps->stdin_pipe,
		      on_stdin_alloc, on_stdin_read);
}

void protocol_set_handler(struct protocol_state *ps,
			  void (*handler)(cJSON *, void *),
			  void *user_data)
{
	ps->on_request = handler;
	ps->user_data = user_data;
}

int protocol_send_response(struct protocol_state *ps, cJSON *response)
{
	char *json;

	(void)ps;

	json = cJSON_PrintUnformatted(response);
	if (!json)
		return -1;

	fprintf(stdout, "%s\n", json);
	fflush(stdout);

	free(json);
	return 0;
}

int protocol_send_notification(struct protocol_state *ps,
			       const char *method, cJSON *params)
{
	cJSON *notif;
	int ret;

	notif = cJSON_CreateObject();
	cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
	cJSON_AddStringToObject(notif, "method", method);
	if (params)
		cJSON_AddItemToObject(notif, "params", params);

	ret = protocol_send_response(ps, notif);
	cJSON_Delete(notif);
	return ret;
}

void protocol_free(struct protocol_state *ps)
{
	if (ps->buf) {
		free(ps->buf);
		ps->buf = NULL;
	}
}
