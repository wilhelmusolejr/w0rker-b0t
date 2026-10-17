#ifndef JSON_CONFIG_H
#define JSON_CONFIG_H
#include <stddef.h>
#include <wchar.h>
typedef enum { JSON_OBJECT, JSON_ARRAY, JSON_STRING, JSON_NUMBER, JSON_BOOL, JSON_NULL } JsonKind;
typedef struct JsonValue {
    JsonKind kind;
    wchar_t *key, *string;
    unsigned number;
    struct JsonValue *child, *next;
} JsonValue;
int json_parse(const wchar_t *text, JsonValue **value, char *error, size_t capacity);
const JsonValue *json_get(const JsonValue *object, const wchar_t *key);
int json_keys(const JsonValue *object, const wchar_t *const *keys, size_t count);
void json_free(JsonValue *value);
#endif
