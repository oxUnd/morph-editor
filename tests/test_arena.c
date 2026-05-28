#include "arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_init_free(void)
{
	struct arena a;

	arena_init(&a);
	assert(a.data == NULL);
	assert(a.offset == 0);
	assert(a.capacity == 0);
	assert(a.next == NULL);
	arena_free(&a);
	printf("  test_init_free: PASS\n");
}

static void test_basic_alloc(void)
{
	struct arena a;
	int *p;
	int i;

	arena_init(&a);
	p = arena_alloc(&a, sizeof(int) * 10);
	assert(p != NULL);
	for (i = 0; i < 10; i++)
		p[i] = i;
	for (i = 0; i < 10; i++)
		assert(p[i] == i);
	arena_free(&a);
	printf("  test_basic_alloc: PASS\n");
}

static void test_multiple_blocks(void)
{
	struct arena a;
	int i;
	void *ptrs[100];

	arena_init(&a);
	for (i = 0; i < 100; i++) {
		ptrs[i] = arena_alloc(&a, 4096);
		assert(ptrs[i] != NULL);
	}
	arena_free(&a);
	printf("  test_multiple_blocks: PASS\n");
}

static void test_reset(void)
{
	struct arena a;
	int *p;

	arena_init(&a);
	p = arena_alloc(&a, sizeof(int) * 10);
	assert(p != NULL);
	p[0] = 42;
	arena_reset(&a);
	/* After reset, offset is 0 but blocks remain */
	p = arena_alloc(&a, sizeof(int) * 10);
	assert(p != NULL);
	p[0] = 99;
	assert(p[0] == 99);
	arena_free(&a);
	printf("  test_reset: PASS\n");
}

static void test_snapshot_rollback(void)
{
	struct arena a;
	size_t snap;
	int *p1, *p2;

	arena_init(&a);
	p1 = arena_alloc(&a, sizeof(int));
	assert(p1 != NULL);
	*p1 = 10;

	snap = arena_snapshot(&a);
	p2 = arena_alloc(&a, sizeof(int));
	assert(p2 != NULL);
	*p2 = 20;

	arena_rollback(&a, snap);
	/* p2 should no longer be valid, but p1 should be */
	assert(*p1 == 10);
	arena_free(&a);
	printf("  test_snapshot_rollback: PASS\n");
}

static void test_arena_set(void)
{
	struct arena_set as;

	arena_set_init(&as);
	arena_alloc(&as.arenas[ARENA_SESSION], 100);
	arena_alloc(&as.arenas[ARENA_IMAGE], 200);
	arena_set_free(&as);
	printf("  test_arena_set: PASS\n");
}

int main(void)
{
	printf("arena tests:\n");
	test_init_free();
	test_basic_alloc();
	test_multiple_blocks();
	test_reset();
	test_snapshot_rollback();
	test_arena_set();
	printf("all arena tests passed!\n");
	return 0;
}
