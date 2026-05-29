#ifndef MORPH_EDITOR_JSON_UTIL_H
#define MORPH_EDITOR_JSON_UTIL_H

#include <stdint.h>
#include "arena.h"
#include "cJSON.h"
#include "bbox.h"
#include "arrow.h"

/*
 * base64_encode - encode binary data to base64 string
 * Returns arena-allocated string, or NULL on error.
 */
char *base64_encode(struct arena *a, const unsigned char *data, int len);

/*
 * base64_decode - decode base64 string to binary data
 * Returns malloc'd buffer (may be large), caller must free.
 * Sets *out_len to decoded length.
 */
unsigned char *base64_decode(const char *str, int *out_len);

/*
 * base64_decode_arena - decode base64 string to arena-allocated buffer
 */
unsigned char *base64_decode_arena(struct arena *a, const char *str,
				   int *out_len);

/*
 * parse_color - parse color string (#RRGGBB or named color)
 * Returns COLOR_RGB value.
 */
uint32_t parse_color(const char *str);

/*
 * bbox_to_json - convert bbox struct to JSON object
 */
cJSON *bbox_to_json(struct bbox *bbox);

/*
 * bbox_from_json - parse bbox from JSON object
 */
struct bbox bbox_from_json(cJSON *json);

/*
 * color_to_hex - convert COLOR_RGB to hex string (#RRGGBB)
 * Returns static buffer, not thread-safe.
 */
const char *color_to_hex(uint32_t color);

cJSON *arrow_to_json(struct arrow *a);
struct arrow arrow_from_json(cJSON *json);

#endif /* MORPH_EDITOR_JSON_UTIL_H */
