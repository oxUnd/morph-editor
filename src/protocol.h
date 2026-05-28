#ifndef MORPH_EDITOR_PROTOCOL_H
#define MORPH_EDITOR_PROTOCOL_H

#include "arena.h"
#include "uv.h"
#include "cJSON.h"

struct protocol_state {
	uv_pipe_t stdin_pipe;
	uv_pipe_t stdout_pipe;
	int reading;
	char *buf;
	int buf_len;
	int buf_cap;
	int content_length;
	void (*on_request)(cJSON *request, void *user_data);
	void *user_data;
};

/*
 * protocol_init - initialize JSON-RPC protocol handler
 */
int protocol_init(struct arena *a, uv_loop_t *loop,
		  struct protocol_state *ps);

/*
 * protocol_send_response - send JSON-RPC response
 */
int protocol_send_response(struct protocol_state *ps, cJSON *response);

/*
 * protocol_send_notification - send JSON-RPC notification
 */
int protocol_send_notification(struct protocol_state *ps,
			       const char *method, cJSON *params);

/*
 * protocol_start_read - start reading from stdin
 */
void protocol_start_read(struct protocol_state *ps);

/*
 * protocol_set_handler - set request handler callback
 */
void protocol_set_handler(struct protocol_state *ps,
			  void (*handler)(cJSON *, void *),
			  void *user_data);

/*
 * protocol_free - free protocol resources
 */
void protocol_free(struct protocol_state *ps);

#endif /* MORPH_EDITOR_PROTOCOL_H */
