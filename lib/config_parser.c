/* A simple INI file parser
 * Copyright (c) 2016, Phillip Berndt
 * Part of pqiv
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef _POSIX_VERSION
#define HAS_MMAP
#endif

#ifdef HAS_MMAP
#include <sys/mman.h>
#endif

#include "config_parser.h"

void config_parser_strip_comments(char *p) {
	char *k;
	int state = 0;

	for(; *p; p++) {
		if(*p == '\n') {
			state = 0;
		}
		else if((*p == '#' || *p == ';') && state == 0) {
			k = strchr(p, '\n');
			if(k) {
				memmove(p, k, strlen(k) + 1);
			}
			else {
				*p = 0;
				break;
			}
		}
		else if(*p != '\t' && *p != ' ') {
			state = 1;
		}
	}
}

void config_parser_parse_file(const char *file_name, config_parser_callback_t callback) {
	int fd = open(file_name, O_RDONLY);
	if(fd < 0) {
		return;
	}

	struct stat stat;
	if(fstat(fd, &stat) < 0) {
		close(fd);
		return;
	}

	char *file_data = NULL;

#ifdef HAS_MMAP
	file_data = mmap(NULL, stat.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if(file_data) {
		config_parser_parse_data(file_data, stat.st_size, callback);
		munmap(file_data, stat.st_size);
		close(fd);
		return;
	}
#endif

	file_data = malloc(stat.st_size);
	if(read(fd, file_data, stat.st_size) == stat.st_size) {
		config_parser_parse_data(file_data, stat.st_size, callback);
	}
	free(file_data);
	close(fd);
}

static void _config_parser_parse_data_invoke_callback(config_parser_callback_t callback, char *section_start, char *section_end, char *key_start, char *key_end, char *data_start, char *data_end) {
	while(key_end > key_start && (*key_end == ' ' || *key_end == '\n' || *key_end == '\r' || *key_end == '\t')) {
		key_end--;
	}
	while(data_end > data_start && (*data_end == ' ' || *data_end == '\n' || *data_end == '\r' || *data_end == '\t')) {
		data_end--;
	}
	if(!data_start || data_end < data_start || *data_start == '\0') {
		return;
	}

	char data_end_restore, section_end_restore, key_end_restore;
	data_end_restore = data_end[1];
	data_end[1] = 0;
	if(section_end) {
		section_end_restore = section_end[1];
		section_end[1] = 0;
	}
	if(key_end) {
		key_end_restore = key_end[1];
		key_end[1] = 0;
	}

	config_parser_value_t value;
	value.chrpval = data_start;
	if(*value.chrpval == 'y' || *value.chrpval == 'Y' || *value.chrpval == 't' || *value.chrpval == 'T') {
		value.intval = 1;
	}
	else {
		value.intval = atoi(value.chrpval);
	}
	value.doubleval = atof(value.chrpval);

	callback(section_start, key_start, &value);

	data_end[1] = data_end_restore;
	if(section_end) {
		section_end[1] = section_end_restore;
	}
	if(key_end) {
		key_end[1] = key_end_restore;
	}
}

void config_parser_parse_data(char *file_data, size_t file_length, config_parser_callback_t callback) {
	enum { DEFAULT, SECTION_IDENTIFIER, COMMENT, VALUE } state = DEFAULT;
	int section_had_keys = 0;
	char *section_start = NULL, *section_end = NULL, *key_start = NULL, *key_end = NULL, *data_start = NULL, *value_start = NULL;

	data_start = file_data;
	char *fp;
	for(fp = file_data; *fp; fp++) {
		if(*fp == ' ' || *fp == '\t') {
			continue;
		}

		if(state == DEFAULT) {
			if(*fp == '[' && key_start == NULL) {
				if(!section_had_keys) {
					_config_parser_parse_data_invoke_callback(callback, section_start, section_end, NULL, NULL, data_start, fp - 1);
				}

				section_had_keys = 0;
				section_start = fp + 1;
				data_start = NULL;
				key_start = NULL;
				state = SECTION_IDENTIFIER;
				continue;
			}
			else if(*fp == ';' || *fp == '#') {
				state = COMMENT;
			}
			else if(*fp == '=' && key_start != NULL) {
				key_end = fp - 1;
				value_start = NULL;
				state = VALUE;
			}
			else if(*fp == '\r' || *fp == '\n') {
				key_start = NULL;
			}
			else {
				if(data_start == NULL) {
					data_start = fp;
				}
				if(key_start == NULL) {
					key_start = fp;
				}
			}
		}
		else if(state == SECTION_IDENTIFIER) {
			if(*fp == ']') {
				section_end = fp - 1;
				state = DEFAULT;
				continue;
			}
		}
		else if(state == COMMENT) {
			if(*fp == '\n') {
				state = DEFAULT;
			}
		}
		else if(state == VALUE) {
			if(value_start == NULL) {
				value_start = fp;
			}

			if(*fp != '\n') {
				continue;
			}
			if(fp[1] == ' ' || fp[1] == '\t') {
				continue;
			}
			state = DEFAULT;
			section_had_keys |= 1;
			_config_parser_parse_data_invoke_callback(callback, section_start, section_end, key_start, key_end, value_start, fp - 1);
			key_start = NULL;
		}
	}

	if(state == VALUE) {
		_config_parser_parse_data_invoke_callback(callback, section_start, section_end, key_start, key_end, value_start, fp - 1);
	}
	else if(state != SECTION_IDENTIFIER) {
		if(!section_had_keys) {
			_config_parser_parse_data_invoke_callback(callback, section_start, section_end, NULL, NULL, data_start, fp - 1);
		}
	}
	else {
		fprintf(stderr, "Info: Failed to parse configuration, parsing finished in an unexpected state (%d).\n", state);
	}
}

#ifdef TEST
void test_cb(char *section, char *key, config_parser_value_t *value) {
	char dup[strlen(value->chrpval)];
	strcpy(dup, value->chrpval);
	config_parser_strip_comments(dup);

	printf("%s.%s: i=%d, f=%f, b=%d, s=\"%s\"\n", section, key, value->intval, value->doubleval, !!value->intval, dup);
}

int main(int argc, char *argv[]) {
	if(argc > 1) {
		config_parser_parse_file(argv[1], &test_cb);
	}
	else {
		char *data = malloc(10240);
		size_t len = 0;
		size_t data_size = 10240;
		while(true) {
			ssize_t red = read(0, &data[len], data_size - len);
			if(red == 0) {
				break;
			}
			len += red;
			if(len == data_size) {
				data_size = data_size + 10240;
				data = realloc(data, data_size);
			}
		}
		data[len] = 0;

		char *data_copy = malloc(data_size);
		memcpy(data_copy, data, data_size);

		config_parser_parse_data(data, data_size, &test_cb);

		if(memcmp(data, data_copy, data_size) != 0) {
			fprintf(stderr, "Warning: Original data changed while processing!\n");
		}

		free(data);
		free(data_copy);
	}
}
#endif
