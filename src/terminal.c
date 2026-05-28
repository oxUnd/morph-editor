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
	/*
	 * iTerm2 3.5+ implements the Kitty graphics protocol,
	 * so route it through the same Kitty path. This avoids
	 * maintaining a second image transport (the old iTerm2
	 * inline OSC-1337 protocol) and gives iTerm2 the same
	 * chunked upload + placement-replace behavior, which
	 * is what makes Kitty rendering smooth.
	 *
	 * Detection: iTerm2 sets TERM_PROGRAM=iTerm.app and
	 * exports LC_TERMINAL_VERSION (e.g. "3.5.10"). Anything
	 * with major>=4 or major==3 && minor>=5 is assumed to
	 * speak Kitty graphics. Older iTerm2 falls back to the
	 * legacy inline path below.
	 */
	{
		const char *prog = getenv("TERM_PROGRAM");
		const char *ver = getenv("LC_TERMINAL_VERSION");
		const char *lc = getenv("LC_TERMINAL");

		if ((prog && strcmp(prog, "iTerm.app") == 0) ||
		    (lc && strcmp(lc, "iTerm2") == 0)) {
			if (ver && ver[0]) {
				int major = atoi(ver);
				int minor = 0;
				const char *dot = strchr(ver, '.');

				if (dot)
					minor = atoi(dot + 1);
				if (major > 3 ||
				    (major == 3 && minor >= 5))
					return 1;
			}
		}
	}
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
