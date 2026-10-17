#include "json_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

typedef struct { const wchar_t *start, *at; unsigned nodes; char *error; size_t capacity; } Parser;
static int bad(Parser *p, const char *message) {
    if (!*p->error) snprintf(p->error, p->capacity, "JSON at character %zu: %s", (size_t)(p->at-p->start), message);
    return 0;
}
static void whitespace(Parser *p) {
    while (*p->at == L' ' || *p->at == L'\t' || *p->at == L'\r' || *p->at == L'\n') ++p->at;
}
static int hex(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') return ch-L'0';
    if (ch >= L'a' && ch <= L'f') return ch-L'a'+10;
    if (ch >= L'A' && ch <= L'F') return ch-L'A'+10;
    return -1;
}
static int unit(Parser *p, wchar_t *out) {
    unsigned n = 0, i;
    for (i = 0; i < 4; ++i) {
        int h = hex(*p->at);
        if (h < 0) return bad(p, "expected four hexadecimal digits");
        n = n*16+(unsigned)h; ++p->at;
    }
    *out = (wchar_t)n; return 1;
}
static wchar_t *string(Parser *p) {
    const wchar_t *scan;
    wchar_t *result, *write;
    size_t length;
    if (*p->at++ != L'"') { bad(p, "expected string"); return NULL; }
    scan = p->at;
    /* Find the raw upper bound without allocating the rest of the document per string. */
    while (*scan && *scan != L'"') {
        if (*scan == L'\\' && scan[1]) ++scan;
        ++scan;
    }
    if (*scan != L'"') { bad(p, "unterminated string"); return NULL; }
    length = (size_t)(scan-p->at);
    result = calloc(length+1, sizeof(*result));
    if (!result) { bad(p, "out of memory"); return NULL; }
    write = result;
    while (*p->at && *p->at != L'"') {
        wchar_t ch = *p->at++;
        if (ch < 32) { bad(p, "unescaped control character"); goto fail; }
        if (ch == L'\\') {
            ch = *p->at++;
            switch (ch) {
                case L'"': case L'\\': case L'/': break;
                case L'b': ch = L'\b'; break;
                case L'f': ch = L'\f'; break;
                case L'n': ch = L'\n'; break;
                case L'r': ch = L'\r'; break;
                case L't': ch = L'\t'; break;
                case L'u': if (!unit(p, &ch)) goto fail; break;
                default: bad(p, "invalid string escape"); goto fail;
            }
        }
        if (!ch) { bad(p, "NUL strings are unsupported"); goto fail; }
        if (ch >= 0xd800 && ch <= 0xdbff) {
            wchar_t low;
            if (*p->at == L'\\' && p->at[1] == L'u') {
                p->at += 2; if (!unit(p, &low)) goto fail;
            } else low = *p->at++;
            if (low < 0xdc00 || low > 0xdfff) { bad(p, "unpaired surrogate"); goto fail; }
            *write++ = ch; *write++ = low;
        } else {
            if (ch >= 0xdc00 && ch <= 0xdfff) { bad(p, "unpaired surrogate"); goto fail; }
            *write++ = ch;
        }
    }
    if (*p->at != L'"') { bad(p, "unterminated string"); goto fail; }
    ++p->at; return result;
fail:
    free(result); return NULL;
}
void json_free(JsonValue *v) {
    while (v) { JsonValue *next = v->next; json_free(v->child); free(v->string); free(v->key); free(v); v = next; }
}
static JsonValue *value(Parser *p, unsigned depth) {
    JsonValue *v;
    whitespace(p);
    if (depth > 16 || ++p->nodes > 20000) { bad(p, "document nesting or item limit exceeded"); return NULL; }
    v = calloc(1, sizeof(*v));
    if (!v) { bad(p, "out of memory"); return NULL; }
    if (*p->at == L'{' || *p->at == L'[') {
        wchar_t close = *p->at++ == L'{' ? L'}' : L']';
        JsonValue **tail = &v->child;
        v->kind = close == L'}' ? JSON_OBJECT : JSON_ARRAY;
        whitespace(p);
        if (*p->at == close) { ++p->at; return v; }
        for (;;) {
            wchar_t *key = NULL;
            JsonValue *item;
            if (v->kind == JSON_OBJECT) {
                const JsonValue *other;
                if (*p->at != L'"' || !(key = string(p))) { bad(p, "expected object key"); goto fail; }
                for (other = v->child; other; other = other->next)
                    if (!wcscmp(other->key, key)) { free(key); bad(p, "duplicate object key"); goto fail; }
                whitespace(p);
                if (*p->at != L':') { free(key); bad(p, "expected colon"); goto fail; }
                ++p->at;
            }
            item = value(p, depth+1);
            if (!item) { free(key); goto fail; }
            item->key = key; *tail = item; tail = &item->next;
            whitespace(p);
            if (*p->at == close) { ++p->at; return v; }
            if (*p->at != L',') { bad(p, "expected comma or closing bracket"); goto fail; }
            ++p->at; whitespace(p);
        }
    }
    if (*p->at == L'"') {
        v->kind = JSON_STRING; v->string = string(p);
        if (!v->string) goto fail;
    } else if (*p->at >= L'0' && *p->at <= L'9') {
        int zero = *p->at == L'0';
        v->kind = JSON_NUMBER;
        do {
            unsigned digit = (unsigned)(*p->at++-L'0');
            if (v->number > (UINT_MAX-digit)/10) { bad(p, "integer overflow"); goto fail; }
            v->number = v->number*10+digit;
            if (zero && *p->at >= L'0' && *p->at <= L'9') { bad(p, "leading zero"); goto fail; }
        } while (*p->at >= L'0' && *p->at <= L'9');
    } else if (!wcsncmp(p->at, L"true", 4)) { v->kind = JSON_BOOL; v->number = 1; p->at += 4; }
    else if (!wcsncmp(p->at, L"false", 5)) { v->kind = JSON_BOOL; p->at += 5; }
    else if (!wcsncmp(p->at, L"null", 4)) { v->kind = JSON_NULL; p->at += 4; }
    else { bad(p, "expected value (configuration numbers must be unsigned integers)"); goto fail; }
    return v;
fail:
    json_free(v); return NULL;
}
int json_parse(const wchar_t *text, JsonValue **out, char *error, size_t capacity) {
    Parser p = {text, text, 0, error, capacity};
    if (!capacity) return 0;
    *error = 0; *out = value(&p, 0);
    if (!*out) return 0;
    whitespace(&p);
    if (*p.at) { bad(&p, "trailing content"); json_free(*out); *out = NULL; return 0; }
    return 1;
}
const JsonValue *json_get(const JsonValue *object, const wchar_t *key) {
    const JsonValue *v;
    if (!object || object->kind != JSON_OBJECT) return NULL;
    for (v = object->child; v; v = v->next) if (!wcscmp(v->key, key)) return v;
    return NULL;
}
int json_keys(const JsonValue *object, const wchar_t *const *keys, size_t count) {
    const JsonValue *v;
    if (!object || object->kind != JSON_OBJECT) return 0;
    for (v = object->child; v; v = v->next) {
        size_t i;
        for (i = 0; i < count; ++i) if (!wcscmp(v->key, keys[i])) break;
        if (i == count) return 0;
    }
    return 1;
}
