#ifndef JSON_HELPER_H
#define JSON_HELPER_H

#include <windows.h>

#define MAX_JSON_STR 4096

typedef struct {
    WCHAR name[MAX_JSON_STR];
    WCHAR content[MAX_JSON_STR];
} LinkItem;

/* Parse JSON array of {name, content} objects from multi-byte input.
   Auto-detects UTF-8 vs system ANSI (GBK on Chinese Windows). */
int parse_json_array(const char* json, LinkItem* items, int max_items, int* count);

/* Parse from pre-decoded wide string */
int parse_json_array_w(const wchar_t* json, LinkItem* items, int max_items, int* count);

/* Check if input looks like valid UTF-8 */
int looks_like_utf8(const char* s);

/* Decode multi-byte string to wide using specified code page */
wchar_t* decode_to_wide(const char* mb, int is_utf8);

#endif
