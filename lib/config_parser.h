/* A simple INI file parser
 * Copyright (c) 2016, Phillip Berndt
 * Part of pqiv
 */

#include <ctype.h>

typedef struct {
	int intval;
	double doubleval;
	char *chrpval;
} config_parser_value_t ;

typedef void (*config_parser_callback_t)(char *section, char *key, config_parser_value_t *value);

void config_parser_parse_file(const char *file_name, config_parser_callback_t callback);
void config_parser_parse_data(char *file_data, size_t file_length, config_parser_callback_t callback);

#define config_parser_tolower(p) if(p) { for(char *n=p ; *n; ++n) *n = tolower(*n); }
