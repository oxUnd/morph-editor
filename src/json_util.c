#include "json_util.h"
#include "arena.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char b64_table[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(struct arena *a, const unsigned char *data, int len)
{
	int out_len;
	char *out;
	int i;
	int j;
	uint32_t v;

	out_len = 4 * ((len + 2) / 3);
	out = arena_alloc(a, out_len + 1);
	if (!out)
		return NULL;

	j = 0;
	for (i = 0; i < len - 2; i += 3) {
		v = ((uint32_t)data[i] << 16) |
		    ((uint32_t)data[i + 1] << 8) |
		    (uint32_t)data[i + 2];
		out[j++] = b64_table[(v >> 18) & 0x3f];
		out[j++] = b64_table[(v >> 12) & 0x3f];
		out[j++] = b64_table[(v >> 6) & 0x3f];
		out[j++] = b64_table[v & 0x3f];
	}
	if (i < len) {
		v = (uint32_t)data[i] << 16;
		if (i + 1 < len)
			v |= (uint32_t)data[i + 1] << 8;
		out[j++] = b64_table[(v >> 18) & 0x3f];
		out[j++] = b64_table[(v >> 12) & 0x3f];
		out[j++] = (i + 1 < len) ?
			   b64_table[(v >> 6) & 0x3f] : '=';
		out[j++] = '=';
	}
	out[j] = '\0';
	return out;
}

static int b64_val(char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

static unsigned char *b64_decode_raw(const char *str, int *out_len)
{
	int slen;
	int out_max;
	unsigned char *out;
	int i;
	int j;
	int v;
	int a, b, c, d;

	slen = strlen(str);
	while (slen > 0 && str[slen - 1] == '=')
		slen--;
	out_max = (slen * 3) / 4 + 4;
	out = malloc(out_max);
	if (!out)
		return NULL;

	j = 0;
	for (i = 0; i + 4 <= slen; i += 4) {
		a = b64_val(str[i]);
		b = b64_val(str[i + 1]);
		c = b64_val(str[i + 2]);
		d = b64_val(str[i + 3]);
		if (a < 0 || b < 0)
			goto err;
		v = (a << 18) | (b << 12) | (MAX(c, 0) << 6) |
		    MAX(d, 0);
		out[j++] = (v >> 16) & 0xff;
		if (c >= 0)
			out[j++] = (v >> 8) & 0xff;
		if (d >= 0)
			out[j++] = v & 0xff;
	}
	/* handle remaining 2-3 chars */
	if (i < slen) {
		a = b64_val(str[i]);
		b = (i + 1 < slen) ? b64_val(str[i + 1]) : 0;
		if (a < 0)
			goto err;
		v = (a << 18) | (b << 12);
		out[j++] = (v >> 16) & 0xff;
		if (i + 2 < slen) {
			c = b64_val(str[i + 2]);
			if (c >= 0) {
				v |= (c << 6);
				out[j++] = (v >> 8) & 0xff;
			}
		}
	}

	*out_len = j;
	return out;
err:
	free(out);
	return NULL;
}

unsigned char *base64_decode(const char *str, int *out_len)
{
	return b64_decode_raw(str, out_len);
}

unsigned char *base64_decode_arena(struct arena *a, const char *str,
				   int *out_len)
{
	unsigned char *raw;
	unsigned char *result;

	raw = b64_decode_raw(str, out_len);
	if (!raw)
		return NULL;

	result = arena_alloc(a, *out_len);
	if (!result) {
		free(raw);
		return NULL;
	}
	memcpy(result, raw, *out_len);
	free(raw);
	return result;
}

uint32_t parse_color(const char *str)
{
	struct named_color {
		const char *name;
		uint32_t value;
	};
	static const struct named_color colors[] = {
		{"red", 0xff0000},
		{"green", 0x00ff00},
		{"blue", 0x0000ff},
		{"white", 0xffffff},
		{"black", 0x000000},
		{"yellow", 0xffff00},
		{"cyan", 0x00ffff},
		{"magenta", 0xff00ff},
		{"orange", 0xff8800},
		{"purple", 0x8800ff},
		{"pink", 0xff88ff},
		{"gray", 0x888888},
		{"grey", 0x888888},
	};
	unsigned int i;
	unsigned int r, g, b;

	if (!str)
		return 0x00ff00;
	if (str[0] == '#' && strlen(str) == 7) {
		if (sscanf(str + 1, "%2x%2x%2x", &r, &g, &b) == 3)
			return COLOR_RGB(r, g, b);
	}
	for (i = 0; i < (int)ARRAY_SIZE(colors); i++) {
		if (strcasecmp(str, colors[i].name) == 0)
			return colors[i].value;
	}
	return 0x00ff00;
}

cJSON *bbox_to_json(struct bbox *bbox)
{
	cJSON *obj;

	obj = cJSON_CreateObject();
	if (!obj)
		return NULL;
	cJSON_AddNumberToObject(obj, "id", bbox->id);
	cJSON_AddNumberToObject(obj, "x", bbox->x);
	cJSON_AddNumberToObject(obj, "y", bbox->y);
	cJSON_AddNumberToObject(obj, "w", bbox->w);
	cJSON_AddNumberToObject(obj, "h", bbox->h);
	if (bbox->label[0])
		cJSON_AddStringToObject(obj, "label", bbox->label);
	cJSON_AddStringToObject(obj, "color", color_to_hex(bbox->color));
	return obj;
}

struct bbox bbox_from_json(cJSON *json)
{
	struct bbox b;
	cJSON *tmp;

	memset(&b, 0, sizeof(b));
	tmp = cJSON_GetObjectItem(json, "id");
	if (tmp)
		b.id = tmp->valueint;
	tmp = cJSON_GetObjectItem(json, "x");
	if (tmp)
		b.x = tmp->valueint;
	tmp = cJSON_GetObjectItem(json, "y");
	if (tmp)
		b.y = tmp->valueint;
	tmp = cJSON_GetObjectItem(json, "w");
	if (tmp)
		b.w = tmp->valueint;
	tmp = cJSON_GetObjectItem(json, "h");
	if (tmp)
		b.h = tmp->valueint;
	tmp = cJSON_GetObjectItem(json, "label");
	if (tmp && tmp->valuestring)
		strncpy(b.label, tmp->valuestring, sizeof(b.label) - 1);
	tmp = cJSON_GetObjectItem(json, "color");
	if (tmp && tmp->valuestring)
		b.color = parse_color(tmp->valuestring);
	return b;
}

const char *color_to_hex(uint32_t color)
{
	static char buf[8];

	snprintf(buf, sizeof(buf), "#%02x%02x%02x",
		 COLOR_R(color), COLOR_G(color), COLOR_B(color));
	return buf;
}
