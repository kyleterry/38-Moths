// vim: noet ts=4 sw=4
#ifdef __clang__
	#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "greshunkel.h"

struct line {
	size_t size;
	char *data;
};

typedef struct line line;

typedef struct _match {
	regoff_t rm_so;
	regoff_t rm_eo;
	size_t len;
	const char *start;
} match_t;

/* Compiled egex vars. */
static regex_t c_var_regex;
static regex_t c_loop_regex;
static regex_t c_filter_regex;
static regex_t c_include_regex;

static const char variable_regex[] = "xXx @([a-zA-Z_0-9]+) xXx";
static const char loop_regex[] = "^\\s+xXx LOOP ([a-zA-Z_]+) ([a-zA-Z_]+) xXx(.*)xXx BBL xXx";
static const char filter_regex[] = "XxX ([a-zA-Z_0-9]+) (.*) XxX";
static const char include_regex[] = "^\\s+xXx SCREAM ([a-zA-Z_]+) xXx";

greshunkel_ctext *gshkl_init_context() {
	greshunkel_ctext *ctext = calloc(1, sizeof(struct greshunkel_ctext));
	ctext->values = vector_new(sizeof(struct greshunkel_tuple), 32);
	ctext->filter_functions = vector_new(sizeof(struct greshunkel_filter), 8);
	return ctext;
}
static greshunkel_ctext *_gshkl_init_child_context(const greshunkel_ctext *parent) {
	greshunkel_ctext *ctext = gshkl_init_context();
	ctext->parent = parent;
	return ctext;
}

static inline int _gshkl_add_var_to_context(greshunkel_ctext *ctext, const greshunkel_tuple *new_tuple) {
	if (!vector_append(ctext->values, new_tuple, sizeof(struct greshunkel_tuple)))
		return 1;
	return 0;
}

static inline int _gshkl_add_var_to_loop(greshunkel_var *loop, const greshunkel_tuple *new_tuple) {
	if (!vector_append(loop->arr, new_tuple, sizeof(struct greshunkel_tuple)))
		return 1;
	return 0;
}

void filter_cleanup(char *result) {
	free(result);
}

int gshkl_add_filter(greshunkel_ctext *ctext,
		const char name[WISDOM_OF_WORDS],
		char *(*filter_func)(const char *argument),
		void (*clean_up)(char *filter_result)) {

	greshunkel_filter new_filter = {
		.filter_func = filter_func,
		.clean_up = clean_up,
		.name = {0}
	};
	strncpy(new_filter.name, name, sizeof(new_filter.name));

	vector_append(ctext->filter_functions, &new_filter, sizeof(struct greshunkel_filter));
	return 0;
}

int gshkl_add_string(greshunkel_ctext *ctext, const char name[WISDOM_OF_WORDS], const char value[MAX_GSHKL_STR_SIZE]) {
	assert(ctext != NULL);

	/* Create a new tuple to hold type and name and shit. */
	greshunkel_tuple _stack_tuple = {
		.name = {0},
		.type = GSHKL_STR,
		.value =  {0}
	};
	strncpy(_stack_tuple.name, name, WISDOM_OF_WORDS);

	/* Copy the value of the string into the var object: */
	greshunkel_var _stack_var = {0};
	strncpy(_stack_var.str, value, MAX_GSHKL_STR_SIZE);
	_stack_var.str[MAX_GSHKL_STR_SIZE] = '\0';

	/* Copy the var object itself into the tuple's var space: */
	memcpy(&_stack_tuple.value, &_stack_var, sizeof(greshunkel_var));

	/* Push that onto our values stack. */
	return _gshkl_add_var_to_context(ctext, &_stack_tuple);
}

int gshkl_add_int(greshunkel_ctext *ctext, const char name[WISDOM_OF_WORDS], const int value) {
	assert(ctext != NULL);

	greshunkel_tuple _stack_tuple = {
		.name = {0},
		.type = GSHKL_STR,
		.value = {0}
	};
	strncpy(_stack_tuple.name, name, WISDOM_OF_WORDS);

	greshunkel_var _stack_var = {0};
	snprintf(_stack_var.str, MAX_GSHKL_STR_SIZE, "%i", value);

	memcpy(&_stack_tuple.value, &_stack_var, sizeof(greshunkel_var));

	return _gshkl_add_var_to_context(ctext, &_stack_tuple);
}

greshunkel_var gshkl_add_array(greshunkel_ctext *ctext, const char name[WISDOM_OF_WORDS]) {
	assert(ctext != NULL);

	greshunkel_tuple _stack_tuple = {
		.name = {0},
		.type = GSHKL_ARR,
		.value = {
			.arr = vector_new(sizeof(greshunkel_tuple), 16)
		}
	};
	strncpy(_stack_tuple.name, name, WISDOM_OF_WORDS);

	_gshkl_add_var_to_context(ctext, &_stack_tuple);
	return _stack_tuple.value;
}

int gshkl_add_int_to_loop(greshunkel_var *loop, const int value) {
	assert(loop != NULL);

	greshunkel_tuple _stack_tuple = {
		.name = {0},
		.type = GSHKL_STR,
		.value = {0}
	};

	greshunkel_var _stack_var = {0};
	snprintf(_stack_var.str, MAX_GSHKL_STR_SIZE, "%i", value);

	memcpy(&_stack_tuple.value, &_stack_var, sizeof(greshunkel_var));

	return _gshkl_add_var_to_loop(loop, &_stack_tuple);
}

int gshkl_add_string_to_loop(greshunkel_var *loop, const char *value) {
	assert(loop != NULL);

	greshunkel_tuple _stack_tuple = {
		.name = {0},
		.type = GSHKL_STR,
		.value = {0}
	};

	greshunkel_var _stack_var = {0};
	strncpy(_stack_var.str, value, MAX_GSHKL_STR_SIZE);
	_stack_var.str[MAX_GSHKL_STR_SIZE] = '\0';

	memcpy(&_stack_tuple.value, &_stack_var, sizeof(union greshunkel_var));

	return _gshkl_add_var_to_loop(loop, &_stack_tuple);
}

static inline void _gshkl_free_arr(greshunkel_tuple *to_free) {
	assert(to_free->type == GSHKL_ARR);
	vector_free((vector *)to_free->value.arr);
}

int gshkl_free_context(greshunkel_ctext *ctext) {
	unsigned int i;
	for (i = 0; i < ctext->values->count; i++) {
		greshunkel_tuple *next = (greshunkel_tuple *)vector_get(ctext->values, i);
		if (next->type == GSHKL_ARR) {
			_gshkl_free_arr(next);
			continue;
		}
	}
	vector_free(ctext->values);
	vector_free(ctext->filter_functions);

	free(ctext);
	return 0;
}

static line read_line(const char *buf) {
	char c = '\0';

	size_t num_read = 0;
	while (1) {
		c = buf[num_read];
		num_read++;
		if (c == '\0' || c == '\n' || c == '\r')
			break;
	}

	line to_return = {
		.size = num_read,
		.data = calloc(1, num_read + 1)
	};
	strncpy(to_return.data, buf, num_read);

	return to_return;
}

/* I'm calling this "vishnu" because i don't actually know what it's for */
static void vishnu(line *new_line_to_add, const match_t match, const char *result, const line *operating_line) {
	char *p;
	const size_t sizes[3] = {match.rm_so, strlen(result), operating_line->size - match.rm_eo};

	new_line_to_add->size = sizes[0] + sizes[1] + sizes[2];
	new_line_to_add->data = p = calloc(1, new_line_to_add->size + 1);

	strncpy(p, operating_line->data, sizes[0]);
	p += sizes[0];
	strncpy(p, result, sizes[1]);
	p += sizes[1];
	strncpy(p, operating_line->data + match.rm_eo, sizes[2]);
}

static int regexec_2_0_beta(const regex_t *preg, const char *string, size_t nmatch, match_t pmatch[]) {
	unsigned int i;
	regmatch_t matches[nmatch];
	if (regexec(preg, string, nmatch, matches, 0) != 0) {
		return 1;
	}
	for (i = 0; i < nmatch; i++) {
		pmatch[i].rm_so = matches[i].rm_so;
		pmatch[i].rm_eo = matches[i].rm_eo;
		pmatch[i].len = matches[i].rm_eo - matches[i].rm_so;
		pmatch[i].start = string + matches[i].rm_so;
	}
	return 0;
}

/* finds a variable or filter by name, returns first matching item or NULL if none found
 * find_values == 1 uses ctext->values, otherwise, ctext->filter_functions */
static const void *find_needle(const greshunkel_ctext *ctext, const char *needle, int find_values) {
	const greshunkel_ctext *current_ctext = ctext;
	while (current_ctext != NULL) {
		vector *current_vector;
		current_vector = (find_values) ? current_ctext->values : current_ctext->filter_functions;
		unsigned int i;
		for (i = 0; i < current_vector->count; i++) {
			const greshunkel_named_item *item = (greshunkel_named_item *)vector_get(current_vector, i);
			assert(item->name != NULL);
			if (strncmp(item->name, needle, strlen(item->name)) == 0) {
				return item;
			}
		}
		current_ctext = current_ctext->parent;
	}
	return NULL;
}

static line
_filter_line(const greshunkel_ctext *ctext, const line *operating_line) {
	line to_return = {0};
	/* Now we match template filters: */
	match_t filter_matches[3];
	/* TODO: More than one filter per line. */
	if (regexec_2_0_beta(&c_filter_regex, operating_line->data, 3, filter_matches) == 0) {
		const match_t function_name = filter_matches[1];
		const match_t argument = filter_matches[2];

		const greshunkel_filter *filter;
		if ((filter = find_needle(ctext, function_name.start, 0))) {

			/* Render the argument out so we can pass it to the filter function. */
			char *rendered_argument = strndup(argument.start, argument.len);

			/* Pass it to the filter function. */
			char *filter_result = filter->filter_func(rendered_argument);

			vishnu(&to_return, filter_matches[0], filter_result, operating_line);

			if (filter->clean_up != NULL)
				filter->clean_up(filter_result);

			free(rendered_argument);
			return to_return;
		}
		assert(filter != NULL);
	}

	/* We didn't match any filters. Just return the operating line. */
	return *operating_line;
}

static line
_interpolate_line(const greshunkel_ctext *ctext, const line current_line) {
	line interpolated_line = {0};
	line new_line_to_add = {0};
	match_t match[2];
	const line *operating_line = &current_line;
	assert(operating_line->data != NULL);

	while (regexec_2_0_beta(&c_var_regex, operating_line->data, 2, match) == 0) {
		const match_t inner_match = match[1];
		assert(inner_match.rm_so != -1 && inner_match.rm_eo != -1);

		const greshunkel_tuple *tuple;
		if ((tuple = find_needle(ctext, inner_match.start, 1)) && tuple->type == GSHKL_STR) {
			vishnu(&new_line_to_add, match[0], tuple->value.str, operating_line);
		} else {
			/* Blow up if we had a variable that wasn't in the context. */
			printf("Did not match a variable that needed to be matched.\n");
			printf("Line: %s\n", operating_line->data);
			assert(tuple != NULL);
			assert(tuple->type == GSHKL_STR);
		}

		free(interpolated_line.data);
		interpolated_line.size = new_line_to_add.size;
		interpolated_line.data = new_line_to_add.data;
		new_line_to_add.size = 0;
		new_line_to_add.data = NULL;
		operating_line = &interpolated_line;

		/* Set the next regex check after this one. */
		memset(match, 0, sizeof(match));
	}
	line filtered_line = _filter_line(ctext, operating_line);
	if (filtered_line.data != interpolated_line.data && interpolated_line.data != NULL)
		free(operating_line->data);

	return filtered_line;
}

static line
_interpolate_loop(const greshunkel_ctext *ctext, const char *buf, size_t *num_read) {
	line to_return = {0};
	*num_read = 0;

	match_t match[4] = {{0}};
	/* TODO: Support loops inside of loops. That probably means a
	 * while loop here. */
	if (regexec_2_0_beta(&c_loop_regex, buf, 4, match) == 0) {
		/* Variables we're going to need: */
		const match_t loop_variable = match[1];
		const match_t variable_name = match[2];
		match_t loop_meat = match[3];
		/* Make sure they were matched: */
		assert(variable_name.rm_so != -1 && variable_name.rm_eo != -1);
		assert(loop_variable.rm_so != -1 && loop_variable.rm_eo != -1);
		assert(loop_meat.rm_so != -1 && loop_meat.rm_eo != -1);

		size_t possible_dif = 0;
		const char *closest_BBL = NULL;
		closest_BBL = strstr(loop_meat.start, "xXx BBL xXx");
		possible_dif = closest_BBL - buf;
		if (possible_dif != (unsigned int)loop_meat.rm_so) {
			loop_meat.rm_eo = possible_dif;
			loop_meat.len = loop_meat.rm_eo - loop_meat.rm_so;
		}

		/* We found a fucking loop, holy shit */
		*num_read = loop_meat.rm_eo + strlen("xXx BBL xXx");

		const greshunkel_tuple *tuple;
		if (!((tuple = find_needle(ctext, variable_name.start, 1)) && tuple->type == GSHKL_ARR)) {
			printf("Did not match a variable that needed to be matched.\n");
			printf("Line: %s\n", buf);
			assert(tuple != NULL);
			assert(tuple->type == GSHKL_ARR);
		}

		line to_render_line;
		to_render_line.data = strndup(loop_meat.start, loop_meat.len);
		to_render_line.size = loop_meat.len;

		vector *cur_vector_p = tuple->value.arr;

		/* This is the thing we're going to render over and over and over again. */
		char *loop_variable_name_rendered = strndup(loop_variable.start, loop_variable.len);

		/* Now we loop through the array incredulously. */
		unsigned int j;
		for (j = 0; j < cur_vector_p->count; j++) {
			const greshunkel_tuple *current_loop_var = vector_get(cur_vector_p, j);
			/* TODO: For now, only strings are supported in arrays. */
			assert(current_loop_var->type == GSHKL_STR);

			/* Recurse contexts until my fucking mind melts. */
			greshunkel_ctext *_temp_ctext = _gshkl_init_child_context(ctext);
			gshkl_add_string(_temp_ctext, loop_variable_name_rendered, current_loop_var->value.str);
			line rendered_piece = _interpolate_line(_temp_ctext, to_render_line);
			gshkl_free_context(_temp_ctext);

			const size_t old_size = to_return.size;
			to_return.size += rendered_piece.size;
			to_return.data = realloc(to_return.data, to_return.size);
			strncpy(to_return.data + old_size, rendered_piece.data, rendered_piece.size);
			free(rendered_piece.data);
		}

		free(loop_variable_name_rendered);
		free(to_render_line.data);
	}

	return to_return;
}
static inline void _compile_regex() {
	int reti = regcomp(&c_var_regex, variable_regex, REG_EXTENDED);
	assert(reti == 0);

	reti = regcomp(&c_loop_regex, loop_regex, REG_EXTENDED);
	assert(reti == 0);

	reti = regcomp(&c_filter_regex, filter_regex, REG_EXTENDED);
	assert(reti == 0);

	reti = regcomp(&c_include_regex, include_regex, REG_EXTENDED);
	assert(reti == 0);
}

static inline void _destroy_regex() {
	regfree(&c_var_regex);
	regfree(&c_loop_regex);
	regfree(&c_filter_regex);
	regfree(&c_include_regex);
}

char *gshkl_render(const greshunkel_ctext *ctext, const char *to_render, const size_t original_size, size_t *outsize) {
	assert(to_render != NULL);
	assert(ctext != NULL);

	/* We start up a new buffer and copy the old one into it: */
	char *rendered = NULL;
	*outsize = 0;

	_compile_regex();

	size_t num_read = 0;
	while (num_read < original_size) {
		line current_line = read_line(to_render + num_read);

		line to_append = {0};
		size_t loop_readahead = 0;
		/* The loop needs to read more than the current line, so we pass
		 * in the offset and just let it go. If it processes more than the
		 * size of the current line, we know it did something.
		 * Append the whole line it gets back. */
		to_append = _interpolate_loop(ctext, to_render + num_read, &loop_readahead);

		/* Otherwise just interpolate the line like normal. */
		if (loop_readahead == 0) {
			to_append = _interpolate_line(ctext, current_line);
			num_read += current_line.size;
		} else {
			num_read += loop_readahead;
		}

		/* Fuck this */
		const size_t old_outsize = *outsize;
		*outsize += to_append.size;
		{
			char *med_buf = realloc(rendered, *outsize);
			if (med_buf == NULL)
				goto error;
			rendered = med_buf;
		}
		strncpy(rendered + old_outsize, to_append.data, to_append.size);
		if (to_append.data != current_line.data)
			free(current_line.data);
		free(to_append.data);
	}
	_destroy_regex();
	rendered[*outsize - 1] = '\0';
	return rendered;

error:
	free(rendered);
	*outsize = 0;
	return NULL;
}
