#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yaml-path.h"

static void
rstrip (char *s)
{
	// This will strip whitespace and explicit document ending
	// to cope with emitter behavior for libyaml < 0.2
	char *pos = s + strlen(s) - 1;
	while (pos > s && (isspace((unsigned char)*pos) || *pos == '.')) {
		*pos = 0;
		pos--;
	}
}

static const char*
yp_event_name (yaml_event_type_t event_type)
{
	switch (event_type) {
	case YAML_NO_EVENT:
		return "no-event";
	case YAML_STREAM_START_EVENT:
		return "stream-start-event";
	case YAML_STREAM_END_EVENT:
		return "stream-end-event";
	case YAML_DOCUMENT_START_EVENT:
		return "document-start-event";
	case YAML_DOCUMENT_END_EVENT:
		return "document-end-event";
	case YAML_ALIAS_EVENT:
		return "alias-event";
	case YAML_SCALAR_EVENT:
		return "scalar-event";
	case YAML_SEQUENCE_START_EVENT:
		return "sequence-start-event";
	case YAML_SEQUENCE_END_EVENT:
		return "sequence-end-event";
	case YAML_MAPPING_START_EVENT:
		return "mapping-start-event";
	case YAML_MAPPING_END_EVENT:
		return "mapping-end-event";
	default:
		return "unknown-event";
	}
}


static char*
yaml;

#define YAML_STRING_LEN 2048
static char
yaml_out[YAML_STRING_LEN] = {0};

static size_t
yaml_out_len = 0;

static yaml_path_filter_mode_t
mode = YAML_PATH_FILTER_RETURN_ALL;

static int
test_result = 0;


int
yp_run (char *path)
{
	yaml_parser_t parser;
	yaml_emitter_t emitter;
	int res = 0;

	yaml_path_t *yp = yaml_path_create();
	yaml_path_parse(yp, path);

	char spath[YAML_STRING_LEN] = {0};
	yaml_path_snprint(yp, spath, YAML_STRING_LEN);
	printf("(%s) ", spath);

	yaml_emitter_initialize(&emitter);
	yaml_parser_initialize(&parser);

	yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, strlen(yaml));
	memset(yaml_out, 0, YAML_STRING_LEN);
	yaml_emitter_set_output_string(&emitter, (unsigned char *)yaml_out, YAML_STRING_LEN, &yaml_out_len);
	yaml_emitter_set_width(&emitter, -1);

	yaml_event_t event;
	yaml_event_type_t prev_event_type, event_type;

	do {
		if (!yaml_parser_parse(&parser, &event)) {
			switch (parser.error) {
			case YAML_MEMORY_ERROR:
				printf("Memory error: Not enough memory for parsing\n");
				break;
			case YAML_READER_ERROR:
				if (parser.problem_value != -1) {
					printf("Reader error: %s: #%X at %ld\n", parser.problem, parser.problem_value, (long)parser.problem_offset);
				} else {
					printf("Reader error: %s at %ld\n", parser.problem, (long)parser.problem_offset);
				}
				break;
			case YAML_SCANNER_ERROR:
				if (parser.context) {
					printf("Scanner error: %s at line %d, column %d\n%s at line %d, column %d\n", parser.context, (int)parser.context_mark.line+1, (int)parser.context_mark.column+1, parser.problem, (int)parser.problem_mark.line+1, (int)parser.problem_mark.column+1);
				} else {
					printf("Scanner error: %s at line %d, column %d\n", parser.problem, (int)parser.problem_mark.line+1, (int)parser.problem_mark.column+1);
				}
				break;
			case YAML_PARSER_ERROR:
				if (parser.context) {
					printf("Parser error: %s at line %d, column %d\n%s at line %d, column %d\n", parser.context, (int)parser.context_mark.line+1, (int)parser.context_mark.column+1, parser.problem, (int)parser.problem_mark.line+1, (int)parser.problem_mark.column+1);
				} else {
					printf("Parser error: %s at line %d, column %d\n", parser.problem, (int)parser.problem_mark.line+1, (int)parser.problem_mark.column+1);
				}
				break;
			default:
				printf("Internal error\n");
				break;
			}
			res = 1;
			goto error;
		} else {
			event_type = event.type;
			if (!yaml_path_filter_event(yp, &parser, &event, mode)) {
				yaml_event_delete(&event);
			} else {
				if (prev_event_type == YAML_DOCUMENT_START_EVENT && event_type == YAML_DOCUMENT_END_EVENT) {
					yaml_event_t null_event= {0};
					yaml_scalar_event_initialize(&null_event, NULL, (yaml_char_t *)"!!null", (yaml_char_t *)"null", 4, 1, 0, YAML_ANY_SCALAR_STYLE);
					yaml_emitter_emit(&emitter, &null_event);
				}
				prev_event_type = event_type;
				if (!yaml_emitter_emit(&emitter, &event)) {
					printf("Error after '%s'\n", yp_event_name(event.type));
					switch (emitter.error)
					{
					case YAML_MEMORY_ERROR:
						printf("Memory error: Not enough memory for emitting\n");
						break;
					case YAML_WRITER_ERROR:
						printf("Writer error: %s\n", emitter.problem);
						break;
					case YAML_EMITTER_ERROR:
						printf("Emitter error: %s\n", emitter.problem);
						break;
					default:
						printf("Internal error\n");
						break;
					}
					res = 2;
					goto error;
				}
			}
		}
	} while (event_type != YAML_STREAM_END_EVENT);

error:
	yaml_parser_delete(&parser);
	yaml_emitter_delete(&emitter);

	yaml_path_destroy(yp);

	return res;
}

#define ASCII_ERR "\033[0;33m"
#define ASCII_RST "\033[0;0m"

void
yp_test (char *path, char *yaml_exp)
{
	printf("%s ", path);
	if (!yp_run(path)) {
		rstrip(yaml_out);
		if (!strcmp(yaml_exp, yaml_out)) {
			printf("(%s): OK\n", yaml_exp);
			return;
		}
		printf(ASCII_ERR"(%s != %s)"ASCII_RST": FAILED\n", yaml_exp, yaml_out);
	}
	test_result++;
}


int main (int argc, char *argv[])
{
	yaml =
		"{"
			"first: {"
				"'Map': {1: '1'},"
				"'Nop': 0,"
				"'Yep': '1',"
				"'Arr': ["
					"[11, 12],"
					"2,"
					"['31', '32'],"
					"[4, 5, 6, 7, 8, 9],"
					"{'k': 'val', 0: 0}"
				"]"
			"},"
			"second: ["
				"{'abc': &anc [1, 2], 'abcdef': 2, 'z': *anc},"
				"{'abc': [3, 4], 'abcdef': 4, 'z': 'zzz'}"
			"]"
		"}";

	//       Path                         Expected filtered YAML result

	mode = YAML_PATH_FILTER_RETURN_ALL;
	yp_test("$.first.Map",               "{1: '1'}");
	yp_test(".first",                    "{'Map': {1: '1'}, 'Nop': 0, 'Yep': '1', 'Arr': [[11, 12], 2, ['31', '32'], [4, 5, 6, 7, 8, 9], {'k': 'val', 0: 0}]}");
	yp_test(".first.Nop",                "0");
	yp_test(".first.Arr",                "[[11, 12], 2, ['31', '32'], [4, 5, 6, 7, 8, 9], {'k': 'val', 0: 0}]");
	yp_test(".first.Arr[0]",             "[11, 12]");
	yp_test(".first.Arr[1]",             "2");
	yp_test(".first.Arr[2][0]",          "'31'");
	yp_test(".first.Arr[:2][0]",         "[11]");
	yp_test(".first.Arr[3][:]",          "[4, 5, 6, 7, 8, 9]");
	yp_test(".first.Arr[4].k",           "'val'");
	yp_test(".first.Arr[:][0]",          "[11, '31', 4]");
	yp_test(".first.Arr[:].k",           "['val']");
	yp_test(".first.Arr[:][2]",          "[6]");
	yp_test(".first.Arr[3][1::2]",       "[5, 7, 9]");
	yp_test(".first.Arr[3][::2]",        "[4, 6, 8]");
	yp_test(".first.Arr[3][:4:2]",       "[4, 6]");
	yp_test(".second[2].abc",            "null");
	yp_test(".second[0:2].abc",          "[&anc [1, 2], [3, 4]]");
	yp_test(".second[0].z",              "*anc");
	yp_test("&anc[0]",                   "1");

	mode = YAML_PATH_FILTER_RETURN_SHALLOW;
	yp_test(".first",                    "{}");
	yp_test(".first.Nop",                "0");
	yp_test(".first.Map",                "{}");

	return test_result;
}
