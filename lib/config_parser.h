/* A simple INI file parser
 * Copyright (c) 2016, Phillip Berndt
 * Part of pqiv
 *
 *
 * This is a simple stream based configuration file parser. Whitespace at the
 * beginning of a line is ignored, except for the continuation of values.
 * Configuration files are separated into sections, marked by [section name]. A
 * section may contain key=value associations, or be any text alternatively.
 * Values may be continued on the next line by indenting its content.
 * "#" and ";" at the beginning of a line mark comments inside key/value sections.
 * To remove comments from plain-text sections, config_parser_strip_comments()
 * may be used.
 *
 * The API is simple, call config_parser_parse_data() or
 * config_parser_parse_file() with a callback function. This function will be
 * called for each section text or key/value association found.
 *
 * The parser makes sure that the file_data remains unchanged if the .._data
 * API is used, but does not restore changes the user performs in the callback.
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

void config_parser_strip_comments(char *p);
#define config_parser_tolower(p) if(p) { for(char *n=p ; *n; ++n) *n = tolower(*n); }
