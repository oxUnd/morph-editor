#include "json_util.h"
#include "arena.h"
#include "bbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_base64_roundtrip(void)
{
	struct arena a;
	const unsigned char data[] = "Hello, World!";
	char *encoded;
	unsigned char *decoded;
	int out_len;

	arena_init(&a);
	encoded = base64_encode(&a, data, sizeof(data) - 1);
	assert(encoded != NULL);

	decoded = base64_decode(encoded, &out_len);
	assert(decoded != NULL);
	assert(out_len == sizeof(data) - 1);
	assert(memcmp(decoded, data, out_len) == 0);

	free(decoded);
	arena_free(&a);
	printf("  test_base64_roundtrip: PASS\n");
}

static void test_parse_color(void)
{
	assert(parse_color("#ff0000") == 0xff0000);
	assert(parse_color("#00ff00") == 0x00ff00);
	assert(parse_color("red") == 0xff0000);
	assert(parse_color("blue") == 0x0000ff);
	printf("  test_parse_color: PASS\n");
}

static void test_bbox_json_roundtrip(void)
{
	struct bbox b;
	struct bbox b2;
	cJSON *json;

	memset(&b, 0, sizeof(b));
	b.x = 10; b.y = 20; b.w = 100; b.h = 80;
	strncpy(b.label, "cat", sizeof(b.label) - 1);
	b.color = 0xff0000;
	b.id = 1;

	json = bbox_to_json(&b);
	assert(json != NULL);

	b2 = bbox_from_json(json);
	assert(b2.x == 10);
	assert(b2.y == 20);
	assert(b2.w == 100);
	assert(b2.h == 80);
	assert(strcmp(b2.label, "cat") == 0);

	cJSON_Delete(json);
	printf("  test_bbox_json_roundtrip: PASS\n");
}

int main(void)
{
	printf("protocol/json tests:\n");
	test_base64_roundtrip();
	test_parse_color();
	test_bbox_json_roundtrip();
	printf("all protocol/json tests passed!\n");
	return 0;
}
