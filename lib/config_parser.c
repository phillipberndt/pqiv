/* A simple INI file parser
 * Copyright (c) 2016, Phillip Berndt
 * Part of pqiv
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <alloca.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "config_parser.h"

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

	char *file_data = mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if(file_data) {
		config_parser_parse_data(file_data, stat.st_size, callback);
		munmap(file_data, stat.st_size);
	}
	else {
		file_data = alloca(stat.st_size);
		if(read(fd, file_data, stat.st_size) != stat.st_size) {
			close(fd);
			return;
		}
		config_parser_parse_data(file_data, stat.st_size, callback);
	}
	close(fd);
}

static void _config_parser_parse_data_invoke_callback(config_parser_callback_t callback, const char *section_start, const char *section_end, const char *key_start, const char *key_end, const char *data_start, const char *data_end) {
	while(key_end > key_start && (*key_end == ' ' || *key_end == '\n' || *key_end == '\r' || *key_end == '\t')) {
		key_end--;
	}

	while(data_end > data_start && (*data_end == ' ' || *data_end == '\n' || *data_end == '\r' || *data_end == '\t')) {
		data_end--;
	}

	if(!data_start || data_end < data_start || *data_start == '\0') {
		return;
	}

	config_parser_value_t value;
#ifdef  __GNUC__
	value.chrpval = strndupa(data_start, data_end - data_start + 1);
#else
	value.chrpval = strndup(data_start, data_end - data_start + 1);
#endif
	if(*value.chrpval == 'y' || *value.chrpval == 'Y' || *value.chrpval == 't' || *value.chrpval == 'T') {
		value.intval = 1;
	}
	else {
		value.intval = atoi(value.chrpval);
	}
	value.doubleval = atof(value.chrpval);

#ifdef __GNUC__
	char *section_name = section_start == section_end ? NULL : strndupa(section_start, section_end - section_start + 1);
	char *key_name = key_start == key_end ? NULL : strndupa(key_start, key_end - key_start + 1);
#else
	char *section_name = section_start == section_end ? NULL : strndup(section_start, section_end - section_start + 1);
	char *key_name = key_start == key_end ? NULL : strndup(key_start, key_end - key_start + 1);
#endif

	callback(section_name, key_name, &value);

#ifndef __GNUC__
	free(section_name);
	free(key_name);
	free(value.chrpval);
#endif
}

void config_parser_parse_data(const char *file_data, size_t file_length, config_parser_callback_t callback) {
	enum { DEFAULT, SECTION_IDENTIFIER, COMMENT, VALUE } state = DEFAULT;
	int section_had_keys = 0;
	const char *section_start = NULL, *section_end = NULL, *key_start = NULL, *key_end = NULL, *data_start = NULL, *value_start = NULL;

	data_start = file_data;
	const char *fp;
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

			if(*fp != ';' && *fp != '#' && *fp != '\n') {
				continue;
			}
			else if(*fp == '\n') {
				if(fp[1] == ' ' || fp[1] == '\t') {
					continue;
				}
				state = DEFAULT;
			}
			else {
				state = COMMENT;
			}
			section_had_keys |= 1;
			_config_parser_parse_data_invoke_callback(callback, section_start, section_end, key_start, key_end, value_start, fp - 1);
			key_start = NULL;
		}
	}

	if(state == VALUE) {
		_config_parser_parse_data_invoke_callback(callback, section_start, section_end, key_start, key_end, value_start, fp - 1);
	}
	else if(state == DEFAULT) {
		if(!section_had_keys) {
			_config_parser_parse_data_invoke_callback(callback, section_start, section_end, NULL, NULL, data_start, fp - 1);
		}
	}
	else {
		fprintf(stderr, "Info: Failed to parse configuration state, parsing finished in an unexpected state.\n");
	}
}

#ifdef TEST
void test_cb(char *section, char *key, config_parser_value_t *value) {
	printf("%s.%s: i=%d, f=%f, b=%d, s=\"%s\"\n", section, key, value->intval, value->doubleval, !!value->intval, value->chrpval);
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

		config_parser_parse_data(data, data_size, &test_cb);

		free(data);
	}
}
#endif
