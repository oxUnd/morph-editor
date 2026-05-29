#include "arena.h"
#include "editor.h"
#include "headless.h"
#include "image.h"
#include "json_util.h"
#include "terminal.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void)
{
	printf("morph-editor [command] [options]\n"
	       "\n"
	       "Commands:\n"
	       "  open <path>                   "
	       "Interactive mode\n"
	       "  info <path>                   "
	       "Output image metadata JSON\n"
	       "  annotate <path> --bbox x,y,w,h [--label text]\n"
	       "  mask <path> --bbox x,y,w,h [--feather N] [--invert]\n"
	       "  crop <path> --bbox x,y,w,h\n"
	       "  draw <path> --rect/text/arrow/circle ...\n"
	       "  transform <path> --resize/rotate/flip/pad ...\n"
	       "  composite --canvas WxH --place img --at x,y ...\n"
	       "  convert <path> --format png\n"
	       "  diff <path1> <path2>\n"
	       "\n"
	       "Global options:\n"
	       "  --output <path>       Output file path\n"
	       "  --output-base64       Output base64 to stdout\n"
	       "  --base64              Input from stdin as base64\n"
	       "  --no-tui              Non-interactive mode\n"
	       "  --serve               JSON-RPC server mode\n"
	       "  --log <path>          Log to file\n"
	       "  --verbose             Verbose logging\n");
}

static char *shift_arg(int *argc, char ***argv)
{
	if (*argc <= 0)
		return NULL;
	(*argc)--;
	return *(*argv)++;
}

int main(int argc, char **argv)
{
	struct arena_set arenas;
	struct headless_args args;
	char *command;
	int no_tui;
	int serve_mode;
	int ret;

	arena_set_init(&arenas);

	/* Skip program name */
	shift_arg(&argc, &argv);

	if (argc <= 0) {
		print_usage();
		return 0;
	}

	command = shift_arg(&argc, &argv);

	memset(&args, 0, sizeof(args));
	no_tui = 0;
	serve_mode = 0;

	/* Parse global options and command-specific args */
	while (argc > 0) {
		char *arg;

		arg = shift_arg(&argc, &argv);

		if (strcmp(arg, "--output") == 0) {
			args.output = shift_arg(&argc, &argv);
		} else if (strcmp(arg, "--output-base64") == 0) {
			args.output_base64 = 1;
		} else if (strcmp(arg, "--base64") == 0) {
			args.input_base64 = 1;
		} else if (strcmp(arg, "--no-tui") == 0) {
			no_tui = 1;
		} else if (strcmp(arg, "--serve") == 0 ||
			   strcmp(arg, "--jsonrpc") == 0) {
			serve_mode = 1;
		} else if (strcmp(arg, "--log") == 0) {
			log_set_file(shift_arg(&argc, &argv));
		} else if (strcmp(arg, "--verbose") == 0) {
			g_verbose = 1;
		} else if (strcmp(arg, "--bbox") == 0) {
			args.bbox_str = shift_arg(&argc, &argv);
		} else if (strcmp(arg, "--label") == 0) {
			args.label = shift_arg(&argc, &argv);
		} else if (strcmp(arg, "--feather") == 0) {
			args.feather_str = shift_arg(&argc, &argv);
		} else if (strcmp(arg, "--invert") == 0) {
			args.invert_mask = 1;
		} else if (strcmp(arg, "--rect") == 0) {
			args.draw_type = "rect";
			args.draw_params = shift_arg(&argc, &argv);
		} else if (strcmp(arg, "--text") == 0) {
			args.draw_type = "text";
			args.draw_params = shift_arg(&argc, &argv);
		} else if (strcmp(arg, "--arrow") == 0) {
			args.draw_type = "arrow";
			args.draw_params = shift_arg(&argc, &argv);
		} else if (strcmp(arg, "--circle") == 0) {
			args.draw_type = "circle";
			args.draw_params = shift_arg(&argc, &argv);
		} else if (strcmp(arg, "--resize") == 0) {
			args.transform_str = "resize:";
			{
				char *val;

				val = shift_arg(&argc, &argv);
				if (val) {
					size_t len;

					len = strlen(val) + 8;
					args.transform_str =
						arena_alloc(
							&arenas.arenas[
								ARENA_SESSION],
							len);
					if (args.transform_str)
						snprintf(
							args.transform_str,
							len,
							"resize:%s",
							val);
				}
			}
		} else if (strcmp(arg, "--rotate") == 0) {
			args.transform_str = "rotate:";
			{
				char *val;

				val = shift_arg(&argc, &argv);
				if (val) {
					size_t len;

					len = strlen(val) + 8;
					args.transform_str =
						arena_alloc(
							&arenas.arenas[
								ARENA_SESSION],
							len);
					if (args.transform_str)
						snprintf(
							args.transform_str,
							len,
							"rotate:%s",
							val);
				}
			}
		} else if (strcmp(arg, "--flip") == 0) {
			char *val;

			val = shift_arg(&argc, &argv);
			if (val) {
				size_t len;

				len = strlen(val) + 6;
				args.transform_str =
					arena_alloc(
						&arenas.arenas[ARENA_SESSION],
						len);
				if (args.transform_str)
					snprintf(args.transform_str, len,
						 "flip:%s", val);
			}
		} else if (strcmp(arg, "--format") == 0) {
			args.format = shift_arg(&argc, &argv);
		} else if (strcmp(arg, "--canvas") == 0) {
			args.composite_str = shift_arg(&argc, &argv);
		} else if (!args.path) {
			args.path = arg;
			if (strcmp(command, "open") == 0 &&
			    args.path_count == 0)
				args.paths[args.path_count++] = arg;
		} else if (strcmp(command, "open") == 0 &&
			   args.path_count < MAX_IMAGES) {
			args.paths[args.path_count++] = arg;
		} else if (strcmp(command, "diff") == 0 &&
			   !args.diff_path) {
			args.diff_path = arg;
		} else {
			fprintf(stderr, "unknown option: %s\n", arg);
			return 1;
		}
	}

	ret = 0;

	if (strcmp(command, "open") == 0 && !no_tui && !serve_mode) {
		struct editor ed;

		editor_init(&ed);
		if (args.path_count > 0) {
			if (args.path_count == 1 &&
			    path_is_video(args.paths[0])) {
				if (editor_open_video(&ed,
						      args.paths[0]) < 0) {
					fprintf(stderr,
						"error: cannot open "
						"video %s\n",
						args.paths[0]);
					editor_free(&ed);
					arena_set_free(&arenas);
					log_close();
					return 1;
				}
			} else if (editor_open_images(
					&ed,
					(const char **)args.paths,
					args.path_count) < 0) {
				int j;

				for (j = 0; j < args.path_count; j++)
					fprintf(stderr,
						"error: cannot open %s\n",
						args.paths[j]);
				editor_free(&ed);
				arena_set_free(&arenas);
				log_close();
				return 1;
			}
		} else if (args.path) {
			const char *single = args.path;

			if (path_is_video(single)) {
				if (editor_open_video(&ed, single) < 0) {
					fprintf(stderr,
						"error: cannot open "
						"video %s\n",
						args.path);
					editor_free(&ed);
					arena_set_free(&arenas);
					log_close();
					return 1;
				}
			} else if (editor_open_images(&ed, &single, 1)
				   < 0) {
				fprintf(stderr,
					"error: cannot open %s\n",
					args.path);
				editor_free(&ed);
				arena_set_free(&arenas);
				log_close();
				return 1;
			}
		}
		ret = editor_run(&ed);
		editor_free(&ed);
	} else if (serve_mode) {
		/* JSON-RPC server mode - Phase 2 */
		fprintf(stderr, "JSON-RPC server mode not yet "
				"implemented\n");
		ret = 1;
	} else {
		/* Headless mode */
		if (!args.path && strcmp(command, "composite") != 0) {
			fprintf(stderr, "error: no input path specified\n");
			print_usage();
			ret = 1;
		} else {
			ret = headless_run(
				&arenas.arenas[ARENA_COMMAND],
				command, &args);
		}
	}

	arena_set_free(&arenas);
	log_close();
	return ret < 0 ? 1 : ret;
}
