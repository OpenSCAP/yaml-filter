#ifndef YAML_PATH_H
#define YAML_PATH_H

#include <yaml.h>


typedef struct yaml_path yaml_path_t;

typedef enum yaml_path_error_type {
	YAML_PATH_ERROR_NONE,
	YAML_PATH_ERROR_NOMEM,
	YAML_PATH_ERROR_PARSE,
	YAML_PATH_ERROR_SECTION,
} yaml_path_error_type_t;

typedef struct yaml_path_error {
	yaml_path_error_type_t type;
	const char *message;
	size_t pos;
} yaml_path_error_t;

typedef enum yaml_path_filter_result {
	YAML_PATH_FILTER_RESULT_OUT,
	YAML_PATH_FILTER_RESULT_IN,
	YAML_PATH_FILTER_RESULT_IN_DANGLING_KEY,
} yaml_path_filter_result_t;


yaml_path_t*
yaml_path_create (void);

int
yaml_path_parse (yaml_path_t *path, char *s_path);

void
yaml_path_destroy (yaml_path_t *path);

const yaml_path_error_t*
yaml_path_error_get (yaml_path_t *path);

yaml_path_filter_result_t
yaml_path_filter_event (yaml_path_t *path, yaml_parser_t *parser, yaml_event_t *event);

size_t
yaml_path_snprint (yaml_path_t *path, char *s, size_t max_len);

#endif//YAML_PATH_H

