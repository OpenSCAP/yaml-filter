#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/queue.h>
#include <assert.h>

#include <yaml.h>

#include "yaml-path.h"


#define YAML_PATH_MAX_SECTION_ITEMS    256

#define _STR(x) #x
#define STR(x) _STR(x)


typedef enum yaml_path_section_type {
	YAML_PATH_SECTION_ROOT,
	YAML_PATH_SECTION_ANCHOR,
	YAML_PATH_SECTION_INDEX,
	YAML_PATH_SECTION_SET,
	YAML_PATH_SECTION_KEY,
	YAML_PATH_SECTION_SELECTION,
} yaml_path_section_type_t;

typedef struct yaml_path_selection_key_raw {
	const char *start;
	size_t len;
} yaml_path_selection_key_raw_t;


typedef struct yaml_path_key {
	const char *key;
	TAILQ_ENTRY(yaml_path_key) entries;
} yaml_path_key_t;

typedef TAILQ_HEAD(path_key_list, yaml_path_key) path_key_list_t;


typedef struct yaml_path_section {
	yaml_path_section_type_t type;
	size_t level;
	union {
		const char *anchor;
		size_t index;
		size_t *set;
		const char *key;
		path_key_list_t selection;
	} data;
	TAILQ_ENTRY(yaml_path_section) entries;

	yaml_node_type_t node_type;
	size_t counter;
	bool valid;
	bool next_valid;
} yaml_path_section_t;

typedef TAILQ_HEAD(path_section_list, yaml_path_section) path_section_list_t;


struct yaml_path {
	path_section_list_t sections_list;
	size_t sections_count;
	size_t current_level;
	size_t start_level;

	yaml_path_error_t error;
};


static size_t
yaml_path_set_snprint (const size_t *set, char *s, size_t max_len)
{
	assert(set != NULL);
	if (s == NULL)
		return -1;
	size_t len = 0;
	if (set[0] == 0) {
		len += snprintf(s, max_len, "[:]");
	} else {
		for (size_t i = 1; i <= set[0]; i++)
			len += snprintf(s + (len < max_len ? len : max_len), max_len - (len < max_len ? len : max_len), "%s%zu", (len ? "," : "["), set[i]);
		len += snprintf(s + (len < max_len ? len : max_len), max_len - (len < max_len ? len : max_len), "]");
	}
	return len;
}

static size_t
yaml_path_selection_snprint (const path_key_list_t *selection, char *s, size_t max_len)
{
	assert(selection != NULL);
	if (s == NULL)
		return -1;
	size_t len = 0;
	yaml_path_key_t *el;
	if TAILQ_EMPTY(selection) {
		len += snprintf(s, max_len, ".*");
	} else {
		TAILQ_FOREACH(el, selection, entries) {
			char quote = strchr(el->key, '\'') ? '"' : '\'';
			len += snprintf(s + (len < max_len ? len : max_len), max_len - (len < max_len ? len : max_len), "%s%c%s%c", (len ? "," : "["), quote, el->key, quote);
		}
		len += snprintf(s + (len < max_len ? len : max_len), max_len - (len < max_len ? len : max_len), "]");
	}
	return len;
}

static bool
yaml_path_set_is_empty (const size_t *set)
{
	assert(set != NULL);
	return set[0] == 0;
}

static bool
yaml_path_set_has_index (const size_t *set, size_t idx)
{
	assert(set != NULL);
	for (size_t i = 1; i <= set[0]; i++)
		if (set[i] == idx)
			return true;
	return false;
}

static bool
yaml_path_selection_is_empty (path_key_list_t *selection)
{
	assert(selection != NULL);
	return TAILQ_EMPTY(selection) ? true : false;
}

static const char*
yaml_path_selection_key_get (path_key_list_t *selection, const char *key)
{
	assert(selection != NULL);
	yaml_path_key_t *el;
	TAILQ_FOREACH(el, selection, entries) {
		if (!strcmp(el->key, key))
			return el->key;
	}
	return NULL;
}

static size_t
yaml_path_selection_keys_add (path_key_list_t *selection, yaml_path_selection_key_raw_t *raw_keys, size_t count)
{
	assert(selection != NULL);
	assert(raw_keys != NULL);
	for (size_t i = 0; i < count; i++) {
		yaml_path_key_t *el = malloc(sizeof(*el));
		if (el == NULL)
			return i;
		TAILQ_INSERT_TAIL(selection, el, entries);
		el->key = strndup(raw_keys[i].start, raw_keys[i].len);
		if (el->key == NULL)
			return i;
	}
	return count;
}

static void
yaml_path_selection_keys_remove (path_key_list_t *selection)
{
	assert(selection != NULL);
	while (!TAILQ_EMPTY(selection)) {
		yaml_path_key_t *el = TAILQ_FIRST(selection);
		TAILQ_REMOVE(selection, el, entries);
		free((void *)el->key);
		free(el);
	}
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
		case YAML_PATH_SECTION_SET:
			free((void *)el->data.set);
			break;
		case YAML_PATH_SECTION_SELECTION:
			yaml_path_selection_keys_remove(&el->data.selection);
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
	if (el != NULL) {
		memset(el, 0, sizeof(*el));
		path->sections_count++;
		el->level = path->sections_count;
		el->type = section_type;
		el->node_type = YAML_NO_NODE;
		TAILQ_INSERT_TAIL(&path->sections_list, el, entries);
		if (el->type == YAML_PATH_SECTION_SELECTION) {
			TAILQ_INIT(&el->data.selection);
		}
	}
	return el;
}

static size_t
yaml_path_section_snprint (yaml_path_section_t *section, char *s, size_t max_len)
{
	assert(section != NULL);
	if (s == NULL)
		return -1;
	size_t len;
	switch (section->type) {
	case YAML_PATH_SECTION_ROOT:
		len = snprintf(s, max_len, "$");
		break;
	case YAML_PATH_SECTION_KEY: {
			char quote = '\0';
			if (strpbrk(section->data.key, "[]().$&*"))
				quote = strchr(section->data.key, '\'') ? '"' : '\'';
			if (quote) {
				len = snprintf(s, max_len, "[%c%s%c]", quote, section->data.key, quote);
			} else {
				len = snprintf(s, max_len, ".%s", section->data.key);
			}
		}
		break;
	case YAML_PATH_SECTION_ANCHOR:
		len = snprintf(s, max_len, "&%s", section->data.anchor);
		break;
	case YAML_PATH_SECTION_INDEX:
		len = snprintf(s, max_len, "[%zu]", section->data.index);
		break;
	case YAML_PATH_SECTION_SET:
		len = yaml_path_set_snprint(section->data.set, s, max_len);
		break;
	case YAML_PATH_SECTION_SELECTION:
		len = yaml_path_selection_snprint(&section->data.selection, s, max_len);
		break;
	default:
		len = snprintf(s, max_len, "<?>");
		break;
	}
	return len;
}

static void
yaml_path_error_set (yaml_path_t *path, yaml_path_error_type_t error_type, const char *message, size_t pos)
{
	assert(path != NULL);
	path->error.type = error_type;
	path->error.message = message;
	path->error.pos = pos;
}

static void
yaml_path_error_clear (yaml_path_t *path)
{
	yaml_path_error_set(path, YAML_PATH_ERROR_NONE, NULL, 0);
}

#define return_with_error(error_type, error_text, error_pos)          \
do {                                                                  \
	yaml_path_error_set(path, error_type, (error_text), (error_pos)); \
	goto error;                                                       \
} while (0)

static void
yaml_path_parse_impl (yaml_path_t *path, char *s_path) {
	char *sp = s_path;
	char *spe = NULL;

	assert(path != NULL);

	if (s_path == NULL || !s_path[0]) {
		yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Path string is NULL or empty", 0);
		return;
	}

	while (*sp != '\0') {
		switch (*sp) {
		case '.':
		case '[':
			if (path->sections_count == 0) {
				yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_ROOT);
				if (sec == NULL)
					return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
			}
			if (*sp == '.') {
				// Key or Selection
				spe = sp + 1;
				if (*spe == '*') {
					// Empty key selection section means that all keys were selected
					yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_SELECTION);
					if (sec == NULL)
						return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
					while (*spe != '.' && *spe != '[' && *spe != '\0')
						spe++;
				} else {
					while (*spe != '.' && *spe != '[' && *spe != '\0')
						spe++;
					if (spe == sp+1)
						return_with_error(YAML_PATH_ERROR_PARSE, "Segment key is missing", sp - s_path);
					yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_KEY);
					if (sec == NULL)
						return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
					sec->data.key = strndup(sp + 1, spe-sp - 1);
					if (sec->data.key == NULL)
						return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (key)", sp - s_path);
				}
				sp = spe-1;
			} else if (*sp == '[') {
				spe = sp + 1;
				if (*spe == '*' && *(spe+1) == ']') {
					// Empty key selection section means that all keys were selected
					yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_SELECTION);
					if (sec == NULL)
						return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
					sp = spe+1;
				} else if (*spe == '\'' || *spe == '"') {
					// Key(s)
					size_t keys_count = 0;
					yaml_path_selection_key_raw_t raw_keys[YAML_PATH_MAX_SECTION_ITEMS] = {{NULL, 0}};
					sp = spe;
					while (*spe != ']' && *spe != '\0') {
						if (keys_count >= YAML_PATH_MAX_SECTION_ITEMS)
							return_with_error(YAML_PATH_ERROR_SECTION, "Segment keys selection has reached the limit of keys: "STR(YAML_PATH_MAX_SECTION_ITEMS), sp - s_path);
						char quote = *spe;
						spe++;
						while (*spe != quote && *spe != '\0')
							spe++;
						if (*spe == '\0')
							return_with_error(YAML_PATH_ERROR_PARSE, "Segment key is invalid (unexpected end of string, missing closing quotation mark)", sp - s_path);
						if (spe == sp+1)
							return_with_error(YAML_PATH_ERROR_PARSE, "Segment key is missing", sp - s_path);
						spe++;
						if (*spe == '\0')
							return_with_error(YAML_PATH_ERROR_PARSE, "Segment key is invalid (unexpected end of string, missing ']')", sp - s_path);
						if (*spe == ']') {
							raw_keys[keys_count].start = sp + 1;
							raw_keys[keys_count].len = spe-sp - 2;
							keys_count++;
						} else if (*spe == ',') {
							spe++;
							if (*spe != '\'' && *spe != '"')
								return_with_error(YAML_PATH_ERROR_PARSE, "Segment keys selection is invalid (invalid character)", spe - s_path);
							raw_keys[keys_count].start = sp + 1;
							raw_keys[keys_count].len = spe-sp - 3;
							keys_count++;
							sp = spe;
						} else {
							return_with_error(YAML_PATH_ERROR_PARSE, "Segment key is invalid (invalid character)", spe - s_path);
						}
					}
					if (keys_count == 1) {
						yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_KEY);
						if (sec == NULL)
							return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
						sec->data.key = strndup(raw_keys[0].start, raw_keys[0].len);
						if (sec->data.key == NULL)
							return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (key)", sp - s_path);
					} else if (keys_count > 1) {
						yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_SELECTION);
						if (sec == NULL)
							return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
						if (yaml_path_selection_keys_add(&sec->data.selection, raw_keys, keys_count) != keys_count)
							return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (keys selection)", sp - s_path);
					}
					sp = spe;
				} else {
					// Indices
					if (*spe == ':' && *(spe+1) == ']') {
						// Set (all-inclusive)
						yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_SET);
						if (sec == NULL)
							return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
						sec->data.set = malloc(sizeof(size_t) * 1);
						if (sec->data.set == NULL)
							return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (set)", sp - s_path);
						sec->data.set[0] = 0;
						sp = spe+1;
					} else {
						while (*spe == ' ' || *spe == '\t')
							spe++;
						if (*spe == '-')
							return_with_error(YAML_PATH_ERROR_PARSE, "Segment index is invalid (negative number)", spe - s_path);
						size_t idx = strtoul(spe, &spe, 10);
						if (*spe == ']') {
							// Index
							yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_INDEX);
							if (sec == NULL)
								return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
							sec->data.index = idx;
							sp = spe;
						} else if (*spe == ',') {
							// Set
							size_t indices[YAML_PATH_MAX_SECTION_ITEMS+1] = {0};
							while (*spe == ',' && spe > sp+1) {
								if (indices[0] >= YAML_PATH_MAX_SECTION_ITEMS)
									return_with_error(YAML_PATH_ERROR_SECTION, "Segment indices set has reached the limit of indices: "STR(YAML_PATH_MAX_SECTION_ITEMS), sp - s_path);
								sp = spe++;
								indices[0]++;
								indices[indices[0]] = idx;
								while (*spe == ' ' || *spe == '\t')
									spe++;
								if (*spe == '-')
									return_with_error(YAML_PATH_ERROR_PARSE, "Segment set index is invalid (negative number)", spe - s_path);
								idx = strtoul(spe, &spe, 10);
							}
							if (*spe == ']' && spe > sp+1) {
								indices[0]++;
								indices[indices[0]] = idx;
								yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_SET);
								if (sec == NULL)
									return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
								sec->data.set = malloc(sizeof(*indices) * (indices[0] + 1));
								if (sec->data.set == NULL)
									return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (set)", sp - s_path);
								memcpy(sec->data.set, indices, sizeof(*indices) * (indices[0] + 1));
								sp = spe;
							} else {
								return_with_error(YAML_PATH_ERROR_PARSE, "Segment set is invalid (invalid character)", spe - s_path);
							}
						} else if (*spe == '\0') {
							return_with_error(YAML_PATH_ERROR_PARSE, "Segment index is invalid (unexpected end of string, missing ']')", spe - s_path);
						} else {
							return_with_error(YAML_PATH_ERROR_PARSE, "Segment index is invalid (invalid character)", spe - s_path);
						}
					}
				}
			}
			break;
		case '&':
			if (path->sections_count == 0) {
				spe = sp + 1;
				while (*spe != '.' && *spe != '[' && *spe != '\0')
					spe++;
				if (spe - sp > 1) {
					yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_ANCHOR);
					if (sec == NULL)
						return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
					sec->data.anchor = strndup(sp+1, spe-sp-1);
					if (sec->data.anchor == NULL)
						return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (anchor)", sp - s_path);
				} else {
					return_with_error(YAML_PATH_ERROR_PARSE, "Segment anchor is invalid (empty)", spe - s_path);
				}
			} else {
				return_with_error(YAML_PATH_ERROR_SECTION, "Anchor segment is only allowed at the beginning of the path", sp - s_path);
			}
			sp = spe - 1;
			break;
		case '$':
			if (path->sections_count == 0) {
				yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_ROOT);
				if (sec == NULL)
					return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
			} else {
				return_with_error(YAML_PATH_ERROR_SECTION, "Root segment is only allowed at the beginning of the path", sp - s_path);
			}
			break;
		default:
			if (path->sections_count == 0) {
				spe = sp + 1;
				// Special beginning of the path (implicit key)
				while (*spe != '.' && *spe != '[' && *spe != '\0')
					spe++;
				yaml_path_section_t *sec = yaml_path_section_create(path, YAML_PATH_SECTION_ROOT);
				if (sec == NULL)
					return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
				sec = yaml_path_section_create(path, YAML_PATH_SECTION_KEY);
				if (sec == NULL)
					return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (section)", sp - s_path);
				sec->data.key = strndup(sp, spe-sp);
				if (sec->data.key == NULL)
					return_with_error(YAML_PATH_ERROR_NOMEM, "Unable to allocate memory (key)", sp - s_path);
				sp = spe-1;
			}
			break;
		}
		sp++;
	}

	if (path->sections_count == 0)
		return_with_error(YAML_PATH_ERROR_SECTION, "Invalid, empty or meaningless path", 0);

	return; // OK

error:
	yaml_path_sections_remove(path);
	if (path->error.type == YAML_PATH_ERROR_NONE)
		yaml_path_error_set(path, YAML_PATH_ERROR_PARSE, "Unable to parse the path string", 0);
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
yaml_path_section_current_is_last (yaml_path_t *path)
{
	assert(path != NULL);
	yaml_path_section_t *sec = yaml_path_section_get_current(path);
	if (sec == NULL)
		return false;
	return sec->level == path->sections_count;
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
yaml_path_section_is_mandatory_container (yaml_path_section_t *sec)
{
	assert(sec != NULL);
	bool res = false;
	if ((sec->type == YAML_PATH_SECTION_SELECTION && sec->node_type == YAML_MAPPING_NODE)
	    ||
	    (sec->type == YAML_PATH_SECTION_SET && sec->node_type == YAML_SEQUENCE_NODE))
		res = true;
	return res;
}

static const char *
yaml_path_filter_event_get_anchor (const yaml_event_t *event)
{
	assert(event != NULL);
	switch(event->type) {
	case YAML_MAPPING_START_EVENT:
		return (const char *)event->data.mapping_start.anchor;
	case YAML_SEQUENCE_START_EVENT:
		return (const char *)event->data.sequence_start.anchor;
	case YAML_SCALAR_EVENT:
		return (const char *)event->data.scalar.anchor;
	default:
		break;
	}
	return NULL;
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


/* Public API -------------------------------------------------------------- */

yaml_path_t*
yaml_path_create (void)
{
	yaml_path_t *ypath = malloc(sizeof(*ypath));
	if (ypath != NULL) {
		memset (ypath, 0, sizeof(*ypath));
		TAILQ_INIT(&ypath->sections_list);
	}
	return ypath;
}

int
yaml_path_parse (yaml_path_t *path, char *s_path)
{
	if (path == NULL)
		return -1;

	yaml_path_sections_remove(path);
	yaml_path_error_clear(path);

	yaml_path_parse_impl(path, s_path);
	if (path->error.type != YAML_PATH_ERROR_NONE)
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

yaml_path_filter_result_t
yaml_path_filter_event (yaml_path_t *path, yaml_parser_t *parser, yaml_event_t *event)
{
	if (path == NULL || parser == NULL || event == NULL || path->sections_count == 0)
		return YAML_PATH_FILTER_RESULT_OUT;

	int res = YAML_PATH_FILTER_RESULT_OUT;

	const char *anchor = yaml_path_filter_event_get_anchor(event);

	if (!path->start_level) {
		switch (yaml_path_section_get_first(path)->type) {
		case YAML_PATH_SECTION_ROOT:
			if (event->type == YAML_DOCUMENT_START_EVENT) {
				path->start_level = 1;
				yaml_path_section_get_first(path)->valid = true;
			}
			break;
		case YAML_PATH_SECTION_ANCHOR:
			if (anchor != NULL && !strcmp(yaml_path_section_get_first(path)->data.anchor, anchor)) {
				path->start_level = path->current_level;
			}
			break;
		default:
			break;
		}
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
			case YAML_NO_NODE:
				if (current_section->type == YAML_PATH_SECTION_ANCHOR) {
					current_section->valid = false;
					if (anchor != NULL && !strcmp(current_section->data.anchor, anchor))
						current_section->valid = true;
				}
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
				} else if (current_section->type == YAML_PATH_SECTION_SELECTION) {
					if (current_section->counter % 2) {
						current_section->valid = current_section->next_valid;
						current_section->next_valid = false;
					} else {
						current_section->next_valid = yaml_path_selection_is_empty(&current_section->data.selection)
						                              || yaml_path_selection_key_get(&current_section->data.selection, (const char *)event->data.scalar.value) != NULL;
						current_section->valid = current_section->next_valid;
					}
				} else {
					current_section->valid = false;
				}
				break;
			case YAML_SEQUENCE_NODE:
				if (current_section->type == YAML_PATH_SECTION_INDEX) {
					current_section->valid = current_section->data.index == current_section->counter;
				} else if (current_section->type == YAML_PATH_SECTION_SET) {
					current_section->valid = yaml_path_set_is_empty(current_section->data.set)
					                         || yaml_path_set_has_index(current_section->data.set, current_section->counter);
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
		res = YAML_PATH_FILTER_RESULT_IN;
		break;
	case YAML_DOCUMENT_START_EVENT:
		if (path->start_level == 1)
			path->current_level++;
		res = YAML_PATH_FILTER_RESULT_IN;
		break;
	case YAML_DOCUMENT_END_EVENT:
		if (path->start_level == 1)
			path->current_level--;
		res = YAML_PATH_FILTER_RESULT_IN;
		break;
	case YAML_MAPPING_START_EVENT:
	case YAML_SEQUENCE_START_EVENT:
		if (current_section) {
			if (yaml_path_section_current_is_last(path))
				if (yaml_path_is_valid(path))
					res = YAML_PATH_FILTER_RESULT_IN;
		} else {
			if (path->current_level > path->start_level) {
				if (yaml_path_is_valid(path))
					res = YAML_PATH_FILTER_RESULT_IN;
			}
		}
		path->current_level++;
		current_section = yaml_path_section_get_current(path);
		if (current_section) {
			current_section->node_type = event->type == YAML_MAPPING_START_EVENT ? YAML_MAPPING_NODE : YAML_SEQUENCE_NODE;
			current_section->counter = 0;
		}
		if (current_section) {
			if (yaml_path_section_is_mandatory_container(current_section) && yaml_path_sections_prev_are_valid(path))
				res = YAML_PATH_FILTER_RESULT_IN;
		}
		break;
	case YAML_MAPPING_END_EVENT:
	case YAML_SEQUENCE_END_EVENT:
		if (current_section) {
			if (yaml_path_section_is_mandatory_container(current_section) && yaml_path_sections_prev_are_valid(path))
				res = YAML_PATH_FILTER_RESULT_IN;

		}
		path->current_level--;
		current_section = yaml_path_section_get_current(path);
		if (current_section) {
			if (yaml_path_section_current_is_last(path))
				if (yaml_path_is_valid(path))
					res = YAML_PATH_FILTER_RESULT_IN;
		} else {
			if (path->current_level > path->start_level) {
				if (yaml_path_is_valid(path))
					res = YAML_PATH_FILTER_RESULT_IN;
			}
		}
		break;
	case YAML_ALIAS_EVENT:
	case YAML_SCALAR_EVENT:
		if (!current_section) {
			if (path->current_level >= path->start_level)
				if (yaml_path_is_valid(path))
					res = YAML_PATH_FILTER_RESULT_IN;
		} else {
			if (yaml_path_section_current_is_last(path) && yaml_path_is_valid(path))
				res = YAML_PATH_FILTER_RESULT_IN;
			if (current_section->valid
				&& current_section->node_type == YAML_MAPPING_NODE
				&& current_section->counter % 2) {
					if (yaml_path_section_is_mandatory_container(current_section) && yaml_path_sections_prev_are_valid(path))
						res = YAML_PATH_FILTER_RESULT_IN_DANGLING_KEY;
				}
		}
		break;
	default:
		break;
	}

	return res;
}
