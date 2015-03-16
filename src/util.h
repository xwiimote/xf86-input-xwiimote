#ifndef UTIL 
#define UTIL

BOOL parse_int_with_default(char const *text, int *out, int default_value);
BOOL parse_double_with_default(char const *text, double *out, double default_value);
BOOL parse_bool_with_default(char const *text, BOOL *out, BOOL default_value);

#endif
