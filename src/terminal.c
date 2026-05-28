#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int query_kitty_support(void)
{
	const char *env;

	env = getenv("KITTY_WINDOW_ID");
	if (env && env[0])
		return 1;
	env = getenv("TERM");
	if (env && strstr(env, "kitty"))
		return 1;
	env = getenv("TERM_PROGRAM");
	if (env && strcmp(env, "kitty") == 0)
		return 1;
	return 0;
}

static int query_iterm2_support(void)
{
	const char *env;

	env = getenv("TERM_PROGRAM");
	if (env && strcmp(env, "iTerm.app") == 0)
		return 1;
	env = getenv("ITERM_SESSION_ID");
	if (env && env[0])
		return 1;
	return 0;
}

static int query_sixel_support(void)
{
	const char *env;

	env = getenv("TERM");
	if (env && strstr(env, "sixel"))
		return 1;
	env = getenv("TERM_PROGRAM");
	if (env && strcmp(env, "mlterm") == 0)
		return 1;
	return 0;
}

enum term_proto terminal_detect(void)
{
	/*
	 * Only use env vars — never send DA1 queries here.
	 * DA1 responses left in stdin will poison termbox2's
	 * event parser after tb_init().
	 */
	if (query_kitty_support())
		return TERM_PROTO_KITTY;
	if (query_iterm2_support())
		return TERM_PROTO_ITERM2;
	if (query_sixel_support())
		return TERM_PROTO_SIXEL;
	return TERM_PROTO_UNKNOWN;
}

const char *terminal_proto_name(enum term_proto proto)
{
	switch (proto) {
	case TERM_PROTO_KITTY:
		return "kitty";
	case TERM_PROTO_SIXEL:
		return "sixel";
	case TERM_PROTO_ITERM2:
		return "iterm2";
	default:
		return "unknown";
	}
}
