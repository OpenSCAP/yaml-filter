#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/queue.h>
#include <yaml.h>

#include "yaml-path.h"


#define YAML_PATH_MAX_SECTION_LEN 1024
#define YAML_PATH_MAX_SECTIONS    255
#define YAML_PATH_MAX_LEN         YAML_PATH_MAX_SECTION_LEN * YAML_PATH_MAX_SECTIONS


typedef enum yaml_path_section_type {
	YAML_PATH_SECTION_KEY,
	YAML_PATH_SECTION_INDEX,
	YAML_PATH_SECTION_SLICE,
	YAML_PATH_SECTION_ANCHOR,
} yaml_path_section_type_t;

typedef struct yaml_path_section {
	yaml_path_section_type_t type;
	int level;
	union {
		const char *key;
		const char *anchor;
		int index;
		struct {int start, end, stride;} slice;
	} data;
	TAILQ_ENTRY(yaml_path_section) entries;

	yaml_node_type_t node_type;
	int counter;
	int valid;
	int next_valid;
} yaml_path_section_t;

typedef TAILQ_HEAD(path_section_list, yaml_path_section) path_section_list_t;

typedef struct yaml_path {
	path_section_list_t sections_list;
	size_t sections_count;

	size_t current_level;
	size_t sequence_level;

	yaml_path_error_t error;
} yaml_path_t;


static void
yaml_path_error_set (yaml_path_t *path, yaml_path_error_type_t error_type, const char *message, size_t pos)
{
	if (path == NULL)
		return;
	path->error.type = error_type;
	path->error.message = message;
	path->error.pos = pos;
}

static void
yaml_path_sections_remove (yaml_path_t *path)
{
	if (path == NULL)
		return;
	while (!TAILQ_EMPTY(&path->sections_list)) {
		yaml_path_section_t *el = TAILQ_FIRST(&path->sections_list);
		TAILQ_REMOVE(&path->sections_list, el, entries);
		path->sections_count--;
		switch (el->type) {
		case YAML_PATH_SECTION_KEY:
			free((void *)el->data.key);
			break;
		case YAML_PATH_SECTION_ANCHOR:
			free((void *)el->data.anchor);
			break;
		case YAML_PATH_SECTION_SLICE:
			if (path->sequence_level == el->level)
				path->sequence_level = 0;
			break;
		default:
			break;
		}
		free(el);
	}
}

static yaml_path_section_t*
yaml_path_section_create (yaml_path_t *path, yaml_path_section_type_t section_type)
{
	yaml_path_section_t *el = malloc(sizeof(*el));
	path->sections_count++;
	el->level = path->sections_count;
	el->type = section_type;
	TAILQ_INSERT_TAIL(&path->sections_list, el, entries);
	if (el->type == YAML_PATH_SECTION_SLICE && !path->sequence_level) {
		path->sequence_level = el->level;
	}
	return el;
}

static size_t
yaml_path_section_snprint (yaml_path_section_t *section, char *s, size_t max_len)
{
	if (s == NULL)
		return -1;
	if (section == NULL)
		return 0;

	size_t len = 0;
	switch (section->type) {
	case YAML_PATH_SECTION_KEY:
		len = snprintf(s, max_len, ".%s", section->data.key);
		break;
	case YAML_PATH_SECTION_ANCHOR:
		len = snprintf(s, max_len, "[&%s]", section->data.anchor);
		break;
	case YAML_PATH_SECTION_INDEX:
		len = snprintf(s, max_len, "[%d]", section->data.index);
		break;
	case YAML_PATH_SECTION_SLICE:
		len = snprintf(s, max_len, "[%d:%d:%d]", section->data.slice.start, section->data.slice.end, section->data.slice.stride);
		break;
	default:
		len = snprintf(s, max_len, "<?>");
		break;
	}
	return len;
}

static void
_parse (yaml_path_t *path, char *s_path) {
	char *sp = s_path;
	char *spe = NULL;

	if (s_path == NULL || !s_path[0]) {
		yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Path is empty", 0);
		return;
	}

	while (*sp != '\0') {
		switch (*sp) {
		case '.':
		case '[':
			if (*sp == '.') {
				// Key
				spe = sp + 1;
				while (*spe != '.' && *spe != '[' && *spe != '\0')
					spe++;
				if (spe == sp+1) {
					yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment key is missing", sp - s_path);
					goto error;
				}
				yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_KEY);
				sec->data.key = strndup(sp + 1, spe-sp - 1);
				sp = spe-1;
			} else if (*sp == '[') {
				spe = sp+1;
				if (*spe == '&') {
					// Anchor
					sp = spe;
					while (*spe != ']' && *spe != '\0')
						spe++;
					if (spe == sp+1) {
						yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment anchor is missing", sp - s_path);
						goto error;
					}
					if (*spe == '\0') {
						yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment anchor is invalid (unxepected end of string, missing ']')", sp - s_path);
						goto error;
					}
					yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_ANCHOR);
					sec->data.anchor = strndup(sp + 1, spe-sp - 1);
					sp = spe;
				} else if (*spe == '\'') {
					// Key
					sp = spe;
					spe++;
					while (*spe != '\'' && *spe != '\0')
						spe++;
					if (spe == sp+1) {
						yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment key is missing", sp - s_path);
						goto error;
					}
					if (*spe == '\0') {
						yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment key is invalid (unxepected end of string, missing ''')", sp - s_path);
						goto error;
					}
					spe++;
					if (*spe == '\0') {
						yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment key is invalid (unxepected end of string, missing ']')", sp - s_path);
						goto error;
					}
					if (*spe != ']') {
						yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment key is invalid (invalid character)", spe - s_path);
						goto error;
					}
					yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_KEY);
					sec->data.key = strndup(sp + 1, spe-sp - 2);
					sp = spe;
				} else {
					// Index or Slice
					int idx = strtol(spe, &spe, 10);
					if (*spe == ']') {
						// Index
						yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_INDEX);
						sec->data.index = idx;
						sp = spe;
					} else if (*spe == ':') {
						// Slice
						int idx_start = idx;
						sp = spe++;
						idx = strtol(spe, &spe, 10);
						if (*spe == ':') {
							int idx_end = (spe == sp+1 ? __INT_MAX__ : idx);
							sp = spe++;
							idx = strtol(spe, &spe, 10);
							if (*spe == ']' && (idx > 0 || spe == sp+1)) {
								yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_SLICE);
								sec->data.slice.start = idx_start;
								sec->data.slice.end = idx_end;
								sec->data.slice.stride = idx > 0 ? idx : 1;
								sp = spe;
							} else if (*spe == ']' && idx <= 0) {
								yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment slice stride can not be less than 1", spe - s_path - 1);
								goto error;
							} else {
								yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment slice stride is invalid (invalid character)", spe - s_path);
								goto error;
							}
						} else if (*spe == ']') {
							yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_SLICE);
							sec->data.slice.start = idx_start;
							sec->data.slice.end = (spe == sp+1 ? __INT_MAX__ : idx);
							sec->data.slice.stride = 1;
							sp = spe;
						} else {
							yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment slice end index is invalid (invalid character)", spe - s_path);
							goto error;
						}
					} else if (*spe == '\0') {
						yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment index is invalid (unxepected end of string, missing ']')", spe - s_path);
						goto error;
					} else {
						yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Segment index is invalid (invalid character)", spe - s_path);
						goto error;
					}
				}
			}
			break;
		default:
			if (path->sections_count == 0) {
				// Key
				spe = sp + 1;
				if (*sp == '$' && (*spe == '.' || *spe == '[')) {
					// Ignore leading '$'
					// TODO: Should we do this?
				} else {
					while (*spe != '.' && *spe != '[' && *spe != '\0')
						spe++;
					yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_KEY);
					sec->data.key = strndup(sp, spe-sp);
					sp = spe-1;
				}
			}
			break;
		}
		sp++;
	}

	if (path->sections_count == 0) {
		yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Invalid path segments", 0);
	}

	return;

error:
	yaml_path_sections_remove(path);
}

static yaml_path_section_t*
yaml_path_section_get_at_level (yaml_path_t *path, size_t level)
{
	if (path == NULL)
		return NULL;
	yaml_path_section_t *el;
	TAILQ_FOREACH(el, &path->sections_list, entries) {
		if (el->level == level)
			return el;
	}
	return NULL;
}

static yaml_path_section_t*
yaml_path_section_get_last (yaml_path_t *path)
{
	if (path == NULL)
		return NULL;
	return yaml_path_section_get_at_level(path, path->sections_count);
}

static yaml_path_section_t*
yaml_path_section_get_current (yaml_path_t *path)
{
	if (path == NULL)
		return NULL;
	return yaml_path_section_get_at_level(path, path->current_level);
}

static int
yaml_path_prev_section_is_valid (yaml_path_t *path)
{
	if (path == NULL)
		return 0;
	yaml_path_section_t *sec = yaml_path_section_get_at_level(path, path->current_level-1);
	if (sec == NULL)
		return -1;
	return sec->valid;
}

static int
yaml_path_all_sections_are_valid (yaml_path_t *path)
{
	if (path == NULL)
		return 0;
	int valid = 1;
	yaml_path_section_t *el;
	TAILQ_FOREACH(el, &path->sections_list, entries) {
		valid = el->valid && valid;
	}
	return valid;
}

static int
yaml_path_section_current_is_last (yaml_path_t *path)
{
	if (path == NULL)
		return 0;
	return path->current_level == path->sections_count;
}

static int
yaml_path_section_current_is_mandatory_sequence (yaml_path_t *path)
{
	if (path == NULL)
		return 0;
	yaml_path_section_t *sec = yaml_path_section_get_current(path);
	if (sec == NULL)
		return 0;
	return (sec->type == YAML_PATH_SECTION_SLICE && path->current_level == path->sequence_level);
}

/* Public */

yaml_path_t*
yaml_path_create (void)
{
	yaml_path_t *ypath = malloc(sizeof(*ypath));

	if (ypath != NULL) {
		TAILQ_INIT(&ypath->sections_list);
		ypath->sections_count = 0;
		ypath->sequence_level = 0;
		ypath->current_level = 0;
	}

	return ypath;
}

int
yaml_path_parse (yaml_path_t *path, char *s_path)
{
	if (path == NULL)
		return -1;

	yaml_path_sections_remove(path);
	memset(&path->error, 0, sizeof(path->error));

	_parse(path, s_path);

	if (path->sections_count == 0)
		return -2;

	return 0;
}

void
yaml_path_destroy (yaml_path_t *path)
{
	if (path == NULL)
		return;
	yaml_path_sections_remove(path);
	free(path);
}

/* API */

const yaml_path_error_t*
yaml_path_error_get (yaml_path_t *path)
{
	if (path == NULL)
		return NULL;
	return &path->error;
}

size_t
yaml_path_snprint (yaml_path_t *path, char *s, size_t max_len)
{
	if (s == NULL)
		return -1;
	if (path == NULL)
		return 0;

	size_t len = 0;

	yaml_path_section_t *el;
	TAILQ_FOREACH(el, &path->sections_list, entries) {
		len += yaml_path_section_snprint(el, s + (len < max_len ? len : max_len), max_len - (len < max_len ? len : max_len));
	}

	return len;
}

int
yaml_path_filter_event (yaml_path_t *path, yaml_parser_t *parser, yaml_event_t *event, yaml_path_filter_mode_t mode)
{
	int res = 0;

	if (path == NULL || parser == NULL || event == NULL)
		goto exit;

	const char *anchor = NULL;
	switch(event->type) {
	case YAML_MAPPING_START_EVENT:
		anchor = (const char *)event->data.mapping_start.anchor;
		break;
	case YAML_SEQUENCE_START_EVENT:
		anchor = (const char *)event->data.sequence_start.anchor;
		break;
	case YAML_SCALAR_EVENT:
		anchor = (const char *)event->data.scalar.anchor;
		break;
	default:
		break;
	}

	yaml_path_section_t *current_section = yaml_path_section_get_current(path);
	if (current_section) {
		switch (event->type) {
		case YAML_MAPPING_START_EVENT:
		case YAML_SEQUENCE_START_EVENT:
		case YAML_ALIAS_EVENT:
		case YAML_SCALAR_EVENT:
			switch (current_section->node_type) {
			case YAML_MAPPING_NODE:
				if (current_section->type == YAML_PATH_SECTION_KEY) {
					if (current_section->counter % 2) {
						current_section->valid = current_section->next_valid;
						current_section->next_valid = 0;
					} else {
						current_section->next_valid = !strcmp(current_section->data.key, (const char *)event->data.scalar.value);
						current_section->valid = 0;
					}
				} else if (current_section->type == YAML_PATH_SECTION_ANCHOR && anchor != NULL) {
					current_section->valid = !strcmp(current_section->data.key, anchor);
				} else {
					current_section->valid = 0;
				}
				break;
			case YAML_SEQUENCE_NODE:
				if (current_section->type == YAML_PATH_SECTION_INDEX) {
					current_section->valid = current_section->data.index == current_section->counter;
				} else if (current_section->type == YAML_PATH_SECTION_SLICE) {
					current_section->valid = current_section->data.slice.start <= current_section->counter &&
					                         current_section->data.slice.end > current_section->counter &&
					                         (current_section->data.slice.start + current_section->counter) % current_section->data.slice.stride == 0;
				} else if (current_section->type == YAML_PATH_SECTION_ANCHOR && anchor != NULL) {
					current_section->valid = !strcmp(current_section->data.key, anchor);
				} else {
					current_section->valid = 0;
				}
				break;
			default:
				break;
			}
			current_section->counter++;
		default:
			break;
		}
		//TODO: DEBUG printf("iv: %d, t: %d, nt: %d, lev: %d\n", current_section->valid, current_section->type, current_section->node_type, current_section->level);
	}

	switch (event->type) {
	case YAML_STREAM_START_EVENT:
	case YAML_STREAM_END_EVENT:
	case YAML_DOCUMENT_START_EVENT:
	case YAML_DOCUMENT_END_EVENT:
	case YAML_NO_EVENT:
		res = 1;
		break;
	case YAML_MAPPING_START_EVENT:
	case YAML_SEQUENCE_START_EVENT:
		current_section = yaml_path_section_get_current(path);
		if (current_section && yaml_path_section_current_is_last(path)) {
			res = current_section->valid && yaml_path_prev_section_is_valid(path);
		} else {
			if (path->current_level > path->sections_count)
				if ((!current_section && mode == YAML_PATH_FILTER_RETURN_ALL) || path->current_level == path->sections_count)
					res = yaml_path_all_sections_are_valid(path);
		};
		path->current_level++;
		current_section = yaml_path_section_get_current(path);
		if (current_section && yaml_path_section_current_is_mandatory_sequence(path)) {
			res = yaml_path_prev_section_is_valid(path);
		}
		if (current_section) {
			current_section->node_type = event->type == YAML_MAPPING_START_EVENT ? YAML_MAPPING_NODE : YAML_SEQUENCE_NODE;
			current_section->counter = 0;
		}
		break;
	case YAML_MAPPING_END_EVENT:
	case YAML_SEQUENCE_END_EVENT:
		if (current_section && yaml_path_section_current_is_mandatory_sequence(path)) {
			res = yaml_path_prev_section_is_valid(path);
		}
		path->current_level--;
		if (path->current_level < path->sections_count && (path->current_level != path->sequence_level || !path->sequence_level))
			break;
		current_section = yaml_path_section_get_current(path);
		if (current_section && yaml_path_section_current_is_last(path)) {
			res = current_section->valid && yaml_path_prev_section_is_valid(path);
		} else {
			if ((!current_section && mode == YAML_PATH_FILTER_RETURN_ALL) || path->current_level == path->sections_count) {
				res = yaml_path_all_sections_are_valid(path);
			}
		}
		break;
	case YAML_ALIAS_EVENT:
	case YAML_SCALAR_EVENT:
		if (!current_section) {
			if (mode == YAML_PATH_FILTER_RETURN_ALL || path->current_level == path->sections_count)
				res = yaml_path_all_sections_are_valid(path);
		} else {
			res = current_section->valid && yaml_path_prev_section_is_valid(path) && yaml_path_section_current_is_last(path);
		}
		break;
	default:
		break;
	}

exit:
	return res;
}
