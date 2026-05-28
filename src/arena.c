#include "arena.h"

#include <stdlib.h>
#include <string.h>

void arena_init(struct arena *a)
{
	memset(a, 0, sizeof(*a));
}

void *arena_alloc(struct arena *a, size_t size)
{
	struct arena *block;
	struct arena *new_block;
	size_t cap;
	void *ptr;

	size = (size + 7) & ~(size_t)7;

	block = a;
	while (block) {
		if (block->capacity - block->offset >= size) {
			ptr = block->data + block->offset;
			block->offset += size;
			return ptr;
		}
		if (!block->next)
			break;
		block = block->next;
	}

	cap = ARENA_BLOCK_SIZE;
	if (size > cap)
		cap = size;

	new_block = malloc(sizeof(struct arena));
	if (!new_block)
		return NULL;
	new_block->data = malloc(cap);
	if (!new_block->data) {
		free(new_block);
		return NULL;
	}
	new_block->offset = 0;
	new_block->capacity = cap;
	new_block->next = NULL;

	if (block)
		block->next = new_block;
	else
		memcpy(a, new_block, sizeof(*new_block));

	ptr = new_block->data + new_block->offset;
	new_block->offset += size;
	return ptr;
}

void arena_reset(struct arena *a)
{
	struct arena *block;

	block = a;
	while (block) {
		block->offset = 0;
		block = block->next;
	}
}

void arena_free(struct arena *a)
{
	struct arena *block;
	struct arena *next;

	/*
	 * First block is embedded (not malloc'd), only free its
	 * data pointer if present.
	 */
	if (a->data) {
		free(a->data);
		a->data = NULL;
	}
	a->offset = 0;
	a->capacity = 0;

	block = a->next;
	a->next = NULL;
	while (block) {
		next = block->next;
		free(block->data);
		free(block);
		block = next;
	}
}

size_t arena_snapshot(struct arena *a)
{
	struct arena *block;
	size_t snap;

	snap = 0;
	block = a;
	while (block) {
		snap += block->offset;
		block = block->next;
	}
	return snap;
}

void arena_rollback(struct arena *a, size_t snap)
{
	struct arena *block;
	size_t accumulated;

	accumulated = 0;
	block = a;
	while (block) {
		if (accumulated + block->offset > snap) {
			block->offset = snap - accumulated;
			/*
			 * Free all blocks after this one since
			 * they were allocated after the snapshot.
			 */
			{
				struct arena *next;
				struct arena *extra;

				next = block->next;
				block->next = NULL;
				extra = next;
				while (extra) {
					next = extra->next;
					free(extra->data);
					free(extra);
					extra = next;
				}
			}
			return;
		}
		accumulated += block->offset;
		block = block->next;
	}
}

void arena_set_init(struct arena_set *as)
{
	int i;

	for (i = 0; i < ARENA_MAX; i++)
		arena_init(&as->arenas[i]);
}

void arena_set_free(struct arena_set *as)
{
	int i;

	for (i = 0; i < ARENA_MAX; i++)
		arena_free(&as->arenas[i]);
}
