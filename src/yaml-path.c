#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/queue.h>
#include <assert.h>

#include <yaml.h>

#include "yaml-path.h"


#define YAML_PATH_MAX_SECTION_LEN 1024
#define YAML_PATH_MAX_SECTIONS    255
#define YAML_PATH_MAX_LEN         YAML_PATH_MAX_SECTION_LEN * YAML_PATH_MAX_SECTIONS


typedef enum yaml_path_section_type {
	YAML_PATH_SECTION_ROOT,
	YAML_PATH_SECTION_ANCHOR,
	YAML_PATH_SECTION_KEY,
	YAML_PATH_SECTION_INDEX,
	YAML_PATH_SECTION_SLICE,
} yaml_path_section_type_t;

typedef struct yaml_path_section {
	yaml_path_section_type_t type;
	size_t level;
	union {
		const char *key;
		const char *anchor;
		int index;
		struct {int start, end, stride;} slice;
	} data;
	TAILQ_ENTRY(yaml_path_section) entries;

	yaml_node_type_t node_type;
	int counter;
	bool valid;
	bool next_valid;
} yaml_path_section_t;

typedef TAILQ_HEAD(path_section_list, yaml_path_section) path_section_list_t;

struct yaml_path {
	path_section_list_t sections_list;
	size_t sections_count;
	size_t sequence_level;

	size_t current_level;
	size_t start_level;

	yaml_path_error_t error;
};


static void
yaml_path_error_set (yaml_path_t *path, yaml_path_error_type_t error_type, const char *message, size_t pos)
{
	assert(path != NULL);
	path->error.type = error_type;
	path->error.message = message;
	path->error.pos = pos;
}

static void
yaml_path_sections_remove (yaml_path_t *path)
{
	assert(path != NULL);
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
	assert(el != NULL);
	memset(el, 0, sizeof(*el));
	path->sections_count++;
	el->level = path->sections_count;
	el->type = section_type;
	el->node_type = YAML_SCALAR_NODE;
	TAILQ_INSERT_TAIL(&path->sections_list, el, entries);
	if (el->type == YAML_PATH_SECTION_SLICE && !path->sequence_level) {
		path->sequence_level = el->level;
	}
	return el;
}

static size_t
yaml_path_section_snprint (yaml_path_section_t *section, char *s, size_t max_len)
{
	assert(section != NULL);
	if (s == NULL)
		return -1;
	size_t len = 0;
	switch (section->type) {
	case YAML_PATH_SECTION_ROOT:
		len = snprintf(s, max_len, "$");
		break;
	case YAML_PATH_SECTION_KEY:
		len = snprintf(s, max_len, ".%s", section->data.key);
		break;
	case YAML_PATH_SECTION_ANCHOR:
		len = snprintf(s, max_len, "&%s", section->data.anchor);
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

	assert(path != NULL);

	if (s_path == NULL || !s_path[0]) {
		yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Path is empty", 0);
		return;
	}

	while (*sp != '\0') {
		switch (*sp) {
		case '.':
		case '[':
			if (path->sections_count == 0) {
				yaml_path_section_create(path, YAML_PATH_SECTION_ROOT);
			}
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
				if (*spe == '\'') {
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
				spe = sp + 1;
				if (*sp == '$' && (*spe == '.' || *spe == '[' || *spe == '\0')) {
					yaml_path_section_create(path, YAML_PATH_SECTION_ROOT);
				} else if (*sp == '&') {
					// Anchor
					sp++;
					while (*spe != '.' && *spe != '[' && *spe != '\0')
						spe++;
					yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_ANCHOR);
					sec->data.anchor = strndup(sp, spe-sp);
				} else {
					// Key
					while (*spe != '.' && *spe != '[' && *spe != '\0')
						spe++;
					yaml_path_section_create(path, YAML_PATH_SECTION_ROOT);
					yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_KEY);
					sec->data.key = strndup(sp, spe-sp);
				}
				sp = spe-1;
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
	assert(path != NULL);
	yaml_path_section_t *el;
	TAILQ_FOREACH(el, &path->sections_list, entries) {
		if (el->level == level)
			return el;
	}
	return NULL;
}

static yaml_path_section_t*
yaml_path_section_get_first (yaml_path_t *path)
{
	assert(path != NULL);
	return yaml_path_section_get_at_level(path, 1);
}

static yaml_path_section_t*
yaml_path_section_get_current (yaml_path_t *path)
{
	assert(path != NULL);
	if (!path->start_level)
		return NULL;
	return yaml_path_section_get_at_level(path, path->current_level - path->start_level + 1);
}

static bool
yaml_path_sections_prev_are_valid (yaml_path_t *path)
{
	assert(path != NULL);
	int valid = true;
	yaml_path_section_t *el;
	TAILQ_FOREACH(el, &path->sections_list, entries) {
		if (el->level < path->current_level - path->start_level + 1)
			valid = el->valid && valid;
	}
	return valid;
}

static bool
yaml_path_section_current_is_last (yaml_path_t *path)
{
	assert(path != NULL);
	yaml_path_section_t *sec = yaml_path_section_get_current(path);
	if (sec == NULL)
		return false;
	return sec->level == path->sections_count;
}

static bool
yaml_path_section_current_is_mandatory_sequence (yaml_path_t *path)
{
	assert(path != NULL);
	yaml_path_section_t *sec = yaml_path_section_get_current(path);
	if (sec == NULL)
		return false;
	return (sec->type == YAML_PATH_SECTION_SLICE && sec->level == path->sequence_level);
}

static bool
yaml_path_is_valid (yaml_path_t *path)
{
	assert(path != NULL);
	bool valid = true;
	yaml_path_section_t *el;
	TAILQ_FOREACH(el, &path->sections_list, entries) {
		valid = el->valid && valid;
	}
	return valid;
}


/* Public */

yaml_path_t*
yaml_path_create (void)
{
	yaml_path_t *ypath = malloc(sizeof(*ypath));

	assert(ypath != NULL);
	memset (ypath, 0, sizeof(*ypath));
	TAILQ_INIT(&ypath->sections_list);

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

	if (!path->start_level) {
		switch (yaml_path_section_get_first(path)->type) {
		case YAML_PATH_SECTION_ROOT:
			if (event->type == YAML_DOCUMENT_START_EVENT) {
				path->start_level = 1;
				yaml_path_section_get_first(path)->valid = true;
			}
			break;
		case YAML_PATH_SECTION_ANCHOR:
			if (anchor != NULL) {
				if (!strcmp(yaml_path_section_get_first(path)->data.anchor, anchor)) {
					path->start_level = path->current_level;
					if (yaml_path_section_get_current(path))
						yaml_path_section_get_current(path)->node_type = YAML_SCALAR_NODE;
				}
			}
			break;
		default:
			//TODO: This path is invalid
			break;
		}
	} else {
		//TODO: ?
	}

	yaml_path_section_t *current_section = yaml_path_section_get_current(path);
	if (current_section) {
		switch (event->type) {
		case YAML_DOCUMENT_START_EVENT:
		case YAML_MAPPING_START_EVENT:
		case YAML_SEQUENCE_START_EVENT:
		case YAML_ALIAS_EVENT:
		case YAML_SCALAR_EVENT:
			switch (current_section->node_type) {
			case YAML_SCALAR_NODE:
				current_section->valid = true;
				break;
			case YAML_MAPPING_NODE:
				if (current_section->type == YAML_PATH_SECTION_KEY) {
					if (current_section->counter % 2) {
						current_section->valid = current_section->next_valid;
						current_section->next_valid = false;
					} else {
						current_section->next_valid = !strcmp(current_section->data.key, (const char *)event->data.scalar.value);
						current_section->valid = false;
					}
				} else {
					current_section->valid = false;
				}
				break;
			case YAML_SEQUENCE_NODE:
				if (current_section->type == YAML_PATH_SECTION_INDEX) {
					current_section->valid = current_section->data.index == current_section->counter;
				} else if (current_section->type == YAML_PATH_SECTION_SLICE) {
					current_section->valid = current_section->data.slice.start <= current_section->counter &&
					                         current_section->data.slice.end > current_section->counter &&
					                         (current_section->data.slice.start + current_section->counter) % current_section->data.slice.stride == 0;
				} else {
					current_section->valid = false;
				}
				break;
			default:
				break;
			}
			current_section->counter++;
		default:
			break;
		}
	}

	switch (event->type) {
	case YAML_STREAM_START_EVENT:
	case YAML_STREAM_END_EVENT:
	case YAML_NO_EVENT:
		res = 1;
		break;
	case YAML_DOCUMENT_START_EVENT:
		if (path->start_level == 1)
			path->current_level++;
		res = 1;
		break;
	case YAML_DOCUMENT_END_EVENT:
		if (path->start_level == 1)
			path->current_level--;
		res = 1;
		break;
	case YAML_MAPPING_START_EVENT:
	case YAML_SEQUENCE_START_EVENT:
		if (current_section) {
			if (yaml_path_section_current_is_last(path))
				res = yaml_path_is_valid(path);
		} else {
			if (path->current_level > path->start_level) {
				if (mode == YAML_PATH_FILTER_RETURN_ALL)
					res = yaml_path_is_valid(path);
			}
		}
		path->current_level++;
		current_section = yaml_path_section_get_current(path);
		if (current_section && yaml_path_section_current_is_mandatory_sequence(path)) {
			res = yaml_path_sections_prev_are_valid(path);
		}
		if (current_section) {
			current_section->node_type = event->type == YAML_MAPPING_START_EVENT ? YAML_MAPPING_NODE : YAML_SEQUENCE_NODE;
			current_section->counter = 0;
		}
		break;
	case YAML_MAPPING_END_EVENT:
	case YAML_SEQUENCE_END_EVENT:
		if (current_section) {
			if (yaml_path_section_current_is_mandatory_sequence(path))
				res = yaml_path_sections_prev_are_valid(path);
		}
		path->current_level--;
		current_section = yaml_path_section_get_current(path);
		if (current_section) {
			if (yaml_path_section_current_is_last(path))
				res = yaml_path_is_valid(path);
		} else {
			if (path->current_level > path->start_level) {
				if (mode == YAML_PATH_FILTER_RETURN_ALL)
					res = yaml_path_is_valid(path);
			}
		}
		break;
	case YAML_ALIAS_EVENT:
	case YAML_SCALAR_EVENT:
		if (!current_section) {
			if ((mode == YAML_PATH_FILTER_RETURN_ALL && path->current_level > path->start_level) || path->current_level == path->start_level)
				res = yaml_path_is_valid(path);
		} else {
			res = yaml_path_is_valid(path) && yaml_path_section_current_is_last(path);
		}
		break;
	default:
		break;
	}

exit:
	return res;
}
