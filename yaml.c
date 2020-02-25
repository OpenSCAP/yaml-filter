#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yaml-path.h"

#define INDENT "  "
#define STRVAL(x) ((x) ? (char*)(x) : "")

void indent(int level)
{
	int i;
	for (i = 0; i < level; i++) {
		printf("%s", INDENT);
	}
}

void print_event(yaml_event_t *event)
{
	static int level = 0;
	
	switch (event->type) {
	case YAML_NO_EVENT:
		indent(level);
		printf("no-event\n");
		break;
	case YAML_STREAM_START_EVENT:
		indent(level++);
		printf("stream-start-event\n");
		break;
	case YAML_STREAM_END_EVENT:
		indent(--level);
		printf("stream-end-event\n");
		break;
	case YAML_DOCUMENT_START_EVENT:
		indent(level++);
		printf("document-start-event\n");
		break;
	case YAML_DOCUMENT_END_EVENT:
		indent(--level);
		printf("document-end-event\n");
		break;
	case YAML_ALIAS_EVENT:
		indent(level);
		printf("alias-event * (anc=\"%s\")\n", STRVAL(event->data.scalar.anchor));
		break;
	case YAML_SCALAR_EVENT:
		indent(level);
		printf("= scalar-event (anc=\"%s\" val=\"%s\", l=%d, t=%s, pl_impl=%d, q_impl=%d, st=%d)\n",
		       STRVAL(event->data.scalar.anchor),
		       STRVAL(event->data.scalar.value),
		       (int)event->data.scalar.length,
		       event->data.scalar.tag,
		       event->data.scalar.plain_implicit, event->data.scalar.quoted_implicit, event->data.scalar.style);
		break;
	case YAML_SEQUENCE_START_EVENT:
		indent(level++);
		printf("[ sequence-start-event (anc=\"%s\", t=%s)\n",
			   STRVAL(event->data.sequence_start.anchor),
		       event->data.sequence_start.tag);
		break;
	case YAML_SEQUENCE_END_EVENT:
		indent(--level);
		printf("] sequence-end-event\n");
		break;
	case YAML_MAPPING_START_EVENT:
		indent(level++);
		printf("{ mapping-start-event\n");
		break;
	case YAML_MAPPING_END_EVENT:
		indent(--level);
		printf("} mapping-end-event\n");
		break;
	}
	if (level < 0) {
		fprintf(stderr, "indentation underflow!\n");
		level = 0;
	}
}

int yaml_parser_parse_and_filter (yaml_parser_t *parser, yaml_event_t *event, yaml_path_t *path)
{
	int valid_event = 0;
	int res;
	do {
		res = yaml_parser_parse(parser, event);
		if (res) {
			printf("=====> ");
			print_event(event);
			if (!yaml_path_filter_event(path, parser, event, YAML_PATH_FILTER_RETURN_ALL)) {
				yaml_event_delete(event);
			} else {
				printf("+------------------------------------------------------------------------------------> ");
				print_event(event);
				valid_event = 1;
			}
		} else {
			break;
		}
	} while (!valid_event && res);
	
	return res;
}

int main(int argc, char *argv[])
{
	yaml_parser_t parser;
	yaml_event_t event;
	yaml_event_type_t event_type;

	yaml_path_t *yp = yaml_path_create();
	//yaml_path_parse(yp, ".fruit.Oop[1]");
	//yaml_path_parse(yp, ".first.Arr[:2][0]"); //.Arr[2][0]
	//yaml_path_parse(yp, ".first.Arr[3][:]");
	//yaml_path_parse(yp, ".first.Map");
	//yaml_path_parse(yp, ".first.Arr[:].k");
	//yaml_path_parse(yp, ".first.Arr[:][2]");
	//yaml_path_parse(yp, ".metadata.name");
	//yaml_path_parse(yp, ".spec.outputs[0:2].name");
	//yaml_path_parse(yp, ".second[0].abc");
	//yaml_path_parse(yp, "&anc[0]");
	yaml_path_parse(yp, ".first.Map");

	//const char *yaml = "2";
	//const char *yaml = "{'el': {'Z': &anc [{'key': 0}, {'item': 1}]}, first: {'Map': &anc {1: '1'}, 'Nop': 'b', 'Yep': '2', 'Arr': [[11,12],2,[31,32],[4, 5, 6],{'k': 1, 0: 0}]}}";
	const char *yaml =
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

	char ypath[255] = {0};
	yaml_path_snprint (yp, ypath, 255);
	printf("%s\n", yaml);
	printf("%s\n\n", ypath);
	
	yaml_parser_initialize(&parser);
	//yaml_parser_set_input_file(&parser, fopen("../openshift-logging-1.yaml", "r"));
	yaml_parser_set_input_string(&parser, (const unsigned char*)yaml, strlen(yaml));

	do {
		if (!yaml_parser_parse_and_filter(&parser, &event, yp))
			goto error;
		event_type = event.type;
		yaml_event_delete(&event);
	} while (event_type != YAML_STREAM_END_EVENT);

	yaml_path_destroy(yp);
	yaml_parser_delete(&parser);
	return 0;

error:
	yaml_path_destroy(yp);
	fprintf(stderr, "Failed to parse: %s\n", parser.problem);
	yaml_parser_delete(&parser);
	return 1;
}
