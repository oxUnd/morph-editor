#ifndef MORPH_EDITOR_TERMINAL_H
#define MORPH_EDITOR_TERMINAL_H

enum term_proto {
	TERM_PROTO_SIXEL,
	TERM_PROTO_KITTY,
	TERM_PROTO_ITERM2,
	TERM_PROTO_UNKNOWN
};

enum term_proto terminal_detect(void);
const char *terminal_proto_name(enum term_proto proto);

#endif /* MORPH_EDITOR_TERMINAL_H */
