#ifndef MORPH_EDITOR_ARENA_H
#define MORPH_EDITOR_ARENA_H

#include <stddef.h>

#define ARENA_BLOCK_SIZE (64 * 1024)

struct arena {
	unsigned char *data;
	size_t offset;
	size_t capacity;
	struct arena *next;
};

enum arena_level {
	ARENA_SESSION,
	ARENA_IMAGE,
	ARENA_FRAME,
	ARENA_COMMAND,
	ARENA_MAX
};

struct arena_set {
	struct arena arenas[ARENA_MAX];
};

/*
 * arena_init - initialize an arena to empty state
 */
void arena_init(struct arena *a);

/*
 * arena_alloc - allocate memory from arena
 * Returns NULL on failure.
 */
void *arena_alloc(struct arena *a, size_t size);

/*
 * arena_reset - reset arena offset, keep blocks for reuse
 */
void arena_reset(struct arena *a);

/*
 * arena_free - free all blocks in the arena
 */
void arena_free(struct arena *a);

/*
 * arena_snapshot - get current offset for later rollback
 */
size_t arena_snapshot(struct arena *a);

/*
 * arena_rollback - rollback arena to a previous snapshot
 */
void arena_rollback(struct arena *a, size_t snap);

/*
 * arena_set_init - initialize all arenas in a set
 */
void arena_set_init(struct arena_set *as);

/*
 * arena_set_free - free all arenas in a set
 */
void arena_set_free(struct arena_set *as);

#endif /* MORPH_EDITOR_ARENA_H */
