#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <yaml.h>

#include "yaml-path.h"


static int
parse_and_emit (yaml_parser_t *parser, yaml_emitter_t *emitter, yaml_path_t *path, yaml_path_filter_mode_t mode, int use_flow_style)
{
	yaml_event_t event;
	yaml_event_type_t event_type, prev_event_type = YAML_NO_EVENT;
	yaml_path_filter_result_t result, prev_result = YAML_PATH_FILTER_RESULT_OUT;

	do {

		if (!yaml_parser_parse(parser, &event)) {
			switch (parser->error) {
			case YAML_MEMORY_ERROR:
				fprintf(stderr, "Memory error: Not enough memory for parsing\n");
				break;
			case YAML_READER_ERROR:
				if (parser->problem_value != -1) {
					fprintf(stderr, "Reader error: %s: #%X at %ld\n", parser->problem, parser->problem_value, (long)parser->problem_offset);
				} else {
					fprintf(stderr, "Reader error: %s at %ld\n", parser->problem, (long)parser->problem_offset);
				}
				break;
			case YAML_SCANNER_ERROR:
				if (parser->context) {
					fprintf(stderr, "Scanner error: %s at line %d, column %d\n%s at line %d, column %d\n", parser->context,
					       (int)parser->context_mark.line+1,(int)parser->context_mark.column+1, parser->problem,
					       (int)parser->problem_mark.line+1, (int)parser->problem_mark.column+1);
				} else {
					fprintf(stderr, "Scanner error: %s at line %d, column %d\n", parser->problem, (int)parser->problem_mark.line+1, (int)parser->problem_mark.column+1);
				}
				break;
			case YAML_PARSER_ERROR:
				if (parser->context) {
					fprintf(stderr, "Parser error: %s at line %d, column %d\n%s at line %d, column %d\n", parser->context,
					       (int)parser->context_mark.line+1, (int)parser->context_mark.column+1, parser->problem,
					       (int)parser->problem_mark.line+1, (int)parser->problem_mark.column+1);
				} else {
					fprintf(stderr, "Parser error: %s at line %d, column %d\n", parser->problem, (int)parser->problem_mark.line+1, (int)parser->problem_mark.column+1);
				}
				break;
			default:
				fprintf(stderr, "Internal error\n");
				break;
			}
			return 1;
		} else {
			event_type = event.type;
			result = yaml_path_filter_event(path, parser, &event, mode);
			if (result == YAML_PATH_FILTER_RESULT_OUT) {
				yaml_event_delete(&event);
			} else {
				if (use_flow_style) {
					switch (event.type) {
					case YAML_SEQUENCE_START_EVENT:
						event.data.sequence_start.style = YAML_FLOW_SEQUENCE_STYLE;
						break;
					case YAML_MAPPING_START_EVENT:
						event.data.mapping_start.style = YAML_FLOW_MAPPING_STYLE;
						break;
					default:
						break;
					}
				}
				if ((prev_event_type == YAML_DOCUMENT_START_EVENT && event_type == YAML_DOCUMENT_END_EVENT)
					|| (prev_result == YAML_PATH_FILTER_RESULT_IN_DANGLING_KEY
						&& (event_type == YAML_MAPPING_END_EVENT
							|| event_type == YAML_SEQUENCE_END_EVENT
							|| result == YAML_PATH_FILTER_RESULT_IN_DANGLING_KEY))) {
					yaml_event_t null_event= {0};
					yaml_scalar_event_initialize(&null_event, NULL, (yaml_char_t *)"!!null", (yaml_char_t *)"null", 4, 1, 0, YAML_ANY_SCALAR_STYLE);
					yaml_emitter_emit(emitter, &null_event);
				}
				prev_result = result;
				prev_event_type = event_type;
				if (!yaml_emitter_emit(emitter, &event)) {
					switch (emitter->error)
					{
					case YAML_MEMORY_ERROR:
						fprintf(stderr, "Memory error: Not enough memory for emitting\n");
						break;
					case YAML_WRITER_ERROR:
						fprintf(stderr, "Writer error: %s\n", emitter->problem);
						break;
					case YAML_EMITTER_ERROR:
						fprintf(stderr, "Emitter error: %s\n", emitter->problem);
						break;
					default:
						fprintf(stderr, "Internal error\n");
						break;
					}
					return 2;
				}
			}
		}
	} while (event_type != YAML_STREAM_END_EVENT);

	return 0;
}


static void
help (void)
{
	printf("yamlp - filtering utility for YAML documents\n");
	printf("\n");
	printf("Usage: yamlp [-F] [-S] [-W <width>] [-f <file>] <path>\n");
	printf("       yamlp -h\n");
	printf("\n");
	printf("The tool will take the input YAML document from <stdin> or a <file> (-f option),\n");
	printf("and it will then return the portion of the document marked with the given <path>.\n");
	printf("\n");
	printf("Options:\n");
	printf("  -f	a filename to get the YAML document from,\n");
	printf("    	<stdin> will be used if omitted;\n");
	printf("\n");
	printf("  -F	forced 'flow' style for the output YAML document;\n");
	printf("\n");
	printf("  -h	help;\n");
	printf("\n");
	printf("  -S	'shallow' filter mode;\n");
	printf("\n");
	printf("  -W	line wrap width, no wrapping if omitted.\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	int flow = 0;
	char *file_name = NULL;
	char *path_string = NULL;
	int wrap = -1;
	yaml_path_filter_mode_t mode = YAML_PATH_FILTER_RETURN_ALL;

	int opt;
	while ((opt = getopt(argc, argv, ":f:W:vhSF")) != -1) {
		switch (opt) {
		case 'h':
			help();
			return 0;
			break;
		case 'F':
			flow = 1;
			break;
		case 'S':
			mode = YAML_PATH_FILTER_RETURN_SHALLOW;
			break;
		case 'W':
			wrap = strtol(optarg, NULL, 10);
			if (!wrap) {
				fprintf(stderr, "Invalid value for wrap width '%s'\n", optarg);
				return 1;
			}
			break;
		case 'f':
			file_name = optarg;
			break;
		case ':':
			fprintf(stderr, "Option needs a value\n");
			return 1;
			break;
		case '?':
			fprintf(stderr, "Unknown option '%c'\n", optopt);
			return 1;
			break;
		}
	}

	for (; optind < argc; optind++) {
		path_string = argv[optind];
	}

	FILE *file = NULL;
	if (file_name != NULL) {
		file = fopen(file_name, "r");
		if (file == NULL) {
			fprintf(stderr, "Unable to open file '%s' (%s)\n", file_name, strerror(errno));
			return 2;
		}
	}

	if (path_string == NULL || path_string[0] == 0) {
		fprintf(stderr, "Empty path\n");
		return 3;
	}

	yaml_path_t *path = yaml_path_create();
	if (yaml_path_parse(path, path_string)) {
		fprintf(stderr, "Invalid path: '%s'\n", path_string);
		fprintf(stderr, "               %*s^ %s [at position %zu]\n", (int)yaml_path_error_get(path)->pos, " ", yaml_path_error_get(path)->message, yaml_path_error_get(path)->pos);
		return 3;
	};

	yaml_parser_t parser;
	yaml_emitter_t emitter;

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, file != NULL ? file : stdin);

	yaml_emitter_initialize(&emitter);
	yaml_emitter_set_output_file(&emitter, stdout);
	yaml_emitter_set_width(&emitter, wrap);

	if (parse_and_emit(&parser, &emitter, path, mode, flow)) {
		return 4;
	}

	yaml_parser_delete(&parser);
	yaml_emitter_delete(&emitter);

	yaml_path_destroy(path);
	if (file != NULL)
		fclose(file);

	return 0;
}
