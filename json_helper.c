#include "json_helper.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Decode entire input to wide string using MultiByteToWideChar
   This supports UTF-8 and system ANSI (GBK on Chinese Windows) */
wchar_t* decode_to_wide(const char* utf8, int is_utf8) {
    int len = MultiByteToWideChar(is_utf8 ? CP_UTF8 : CP_ACP, 0, utf8, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t* ws = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!ws) return NULL;
    MultiByteToWideChar(is_utf8 ? CP_UTF8 : CP_ACP, 0, utf8, -1, ws, len);
    return ws;
}

/* Check if bytes look like valid UTF-8 */
int looks_like_utf8(const char* s) {
    if (!s || !*s) return 1;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        if (*p < 0x80) { p++; continue; }
        if (*p < 0xC0) return 0; /* stray continuation byte */
        if (*p < 0xE0) { if (p[1] && (p[1]&0xC0)==0x80) { p+=2; continue; } return 0; }
        if (*p < 0xF0) { if (p[1]&&(p[1]&0xC0)==0x80 && p[2]&&(p[2]&0xC0)==0x80) { p+=3; continue; } return 0; }
        if (*p < 0xF8) { if (p[1]&&(p[1]&0xC0)==0x80 && p[2]&&(p[2]&0xC0)==0x80 && p[3]&&(p[3]&0xC0)==0x80) { p+=4; continue; } return 0; }
        return 0;
    }
    return 1;
}

/* Skip whitespace in wide string */
static const wchar_t* skip_w(const wchar_t* p) {
    while (*p && *p <= 32) p++;
    return p;
}

/* Decode JSON escape sequences from wide string to wide buffer */
static const wchar_t* dec_w(const wchar_t* p, wchar_t* out, int max) {
    if (*p != L'"') return NULL;
    p++;
    int pos = 0;
    while (*p && *p != L'"' && pos < max - 1) {
        if (*p == 0x5C) {
            p++;
            switch (*p) {
                case L'"':  out[pos++] = L'"'; break;
                case 0x5C: out[pos++] = 0x5C; break;
                case L'/':  out[pos++] = L'/'; break;
                case L'b':  out[pos++] = 8; break;
                case L'f':  out[pos++] = 12; break;
                case L'n':  out[pos++] = 10; break;
                case L'r':  out[pos++] = 13; break;
                case L't':  out[pos++] = 9; break;
                case L'u': {
                    if (p[1] && p[2] && p[3] && p[4]) {
                        wchar_t code = 0;
                        for (int i=0; i<4; i++) {
                            code <<= 4;
                            wchar_t c2 = p[i+1];
                            if (c2 >= '0' && c2 <= '9') code |= c2 - '0';
                            else if (c2 >= 'a' && c2 <= 'f') code |= c2 - 'a' + 10;
                            else if (c2 >= 'A' && c2 <= 'F') code |= c2 - 'A' + 10;
                        }
                        p += 4;
                        out[pos++] = code;
                    }
                    break;
                }
                default: out[pos++] = *p; break;
            }
            p++;
        } else {
            out[pos++] = *p;
            p++;
        }
    }
    out[pos] = 0;
    if (*p == L'"') p++;
    return p;
}

int parse_json_array_w(const wchar_t* json, LinkItem* items, int max_items, int* count) {
    *count = 0;
    const wchar_t* p = skip_w(json);
    if (*p != L'[') return 0;
    p = skip_w(p + 1);

    wchar_t key[64], val[MAX_JSON_STR];

    while (*p && *p != L']' && *count < max_items) {
        p = skip_w(p);
        if (*p != L'{') break;
        p = skip_w(p + 1);

        LinkItem item;
        memset(&item, 0, sizeof(item));
        int gn = 0, gc = 0;

        while (*p && *p != L'}') {
            p = skip_w(p);
            if (*p != L'"') { p++; continue; }
            p = dec_w(p, key, 64);
            if (!p) return 0;
            p = skip_w(p);
            if (*p != L':') { p++; continue; }
            p = skip_w(p + 1);

            if (*p == L'"') {
                p = dec_w(p, val, MAX_JSON_STR);
                if (!p) return 0;
                if (wcscmp(key, L"name") == 0) {
                    wcsncpy(item.name, val, MAX_JSON_STR - 1);
                    gn = 1;
                } else if (wcscmp(key, L"content") == 0) {
                    wcsncpy(item.content, val, MAX_JSON_STR - 1);
                    gc = 1;
                }
            }
            p = skip_w(p);
            if (*p == L',') p = skip_w(p + 1);
        }
        if (*p == L'}') {
            p = skip_w(p + 1);
            if (gn && gc) { items[*count] = item; (*count)++; }
        }
        p = skip_w(p);
        if (*p == L',') p = skip_w(p + 1);
    }
    return 1;
}

/* Parse JSON from multi-byte input - auto-detect UTF-8 vs system ANSI */
int parse_json_array(const char* json, LinkItem* items, int max_items, int* count) {
    int is_utf8 = looks_like_utf8(json);
    wchar_t* ws = decode_to_wide(json, is_utf8);
    if (!ws) return 0;
    int ret = parse_json_array_w(ws, items, max_items, count);
    free(ws);
    return ret;
}
