/*
 * File: ini.h
 *
 * INI loader/parser
 *
 * Simple to use INI file loader that allows:
 * - Key/value pairs using strings and numbers. (e.g. WINDOW_WIDTH = 720)
 * - Commented lines using `#`. (e.g. #this is a comment)
 */

#ifndef INI_H
#define INI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct IniData IniData;

// ini_load: Load an INI file into memory given a file_path, or initialize an
// empty IniData struct by setting file_path to NULL.
IniData *ini_load(const char *file_path);
// ini_unload: Free all internal heap allocations and IniData pointer.
void ini_unload(IniData *data);
// ini_dump: Write the contents of a IniData struct to a given file_path.
bool ini_dump(IniData *data, const char *file_path);
// ini_get: Retrieve the value associated with the given key.
bool ini_get(IniData *data, const char *key, double *value);
// ini_set: Store a value in the entry associated with the given key. Create
// a new entry entry if the key does not already exist.
bool ini_set(IniData *data, const char *key, double value);

#ifdef INI_IMPLEMENTATION
#ifndef INI_IMPL_GUARD
#define INI_IMPL_GUARD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOAD_FACTOR 0.75
#define GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)

typedef struct {
    uint32_t hash;
    size_t len;
    char *chars;
} IniKey;

typedef struct {
    IniKey key;
    double value;
} IniEntry;

struct IniData {
    size_t count;
    size_t capacity;
    IniEntry *entries;
};

static char *read_file(const char *file_path)
{
    FILE *fp = fopen(file_path, "rb");
    if (fp == NULL) return NULL;

    fseek(fp, 0L, SEEK_END);
    size_t file_size = ftell(fp);
    rewind(fp);

    char *source = (char *)malloc(sizeof(char) * (file_size + 1));
    if (source == NULL) {
        fclose(fp);
        return NULL;
    }

    size_t bytes_read = fread(source, sizeof(char), file_size, fp);
    if (bytes_read < file_size) {
        free(source);
        fclose(fp);
        return NULL;
    }
    source[file_size] = '\0';

    fclose(fp);
    return source;
}

// Using the FNV-1a hash function algorithm.
static uint32_t hash_string(const char *key, size_t len)
{
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }

    return hash;
}

static bool match_keys(IniKey *key1, IniKey *key2)
{
    return key1->len == key2->len &&
           key1->hash == key2->hash &&
           memcmp(key1->chars, key2->chars, key1->len) == 0;
}

static IniEntry *find_entry(IniEntry *entries, IniKey *key, size_t capacity)
{
    uint32_t index = key->hash & (capacity - 1);

    for (;;) {
        IniEntry *e = &entries[index];

        if (e->key.chars != NULL) {
            if (match_keys(&e->key, key)) return e;
        } else return e;

        index = (index + 1) & (capacity - 1);
    }
}

static bool grow_entries(IniData *data)
{
    size_t new_cap = GROW_CAPACITY(data->capacity);
    size_t new_size = sizeof(IniEntry) * new_cap;
    IniEntry *entries = (IniEntry *)realloc(data->entries, new_size);
    if (entries == NULL) return false;

    for (size_t i = 0; i < new_cap; i++) entries[i].key.chars = NULL;

    for (size_t i = 0; i < data->capacity; i++) {
        IniEntry *e = &data->entries[i];
        if (e->key.chars == NULL) continue;

        IniEntry *dest = find_entry(entries, &e->key, new_cap);
        dest->key = e->key;
        dest->value = e->value;
    }

    if (data->entries != NULL) free(data->entries);
    data->entries = entries;
    data->capacity = new_cap;

    return true;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_whitespace(char c)
{
    return c == ' '  || c == '\n' ||
           c == '\t' || c == '\t';
}

static char *skip_whitespace(char *begin)
{
    char *end = begin;

    for (;;) {
        if (is_whitespace(*end)) {
            end++;
        } else if (*end == '#') {
            while (*end != '\n' && *end != '\0') end++;
        } else break;
    }

    return end;
}

static IniKey make_key(const char *chars)
{
    size_t len = strlen(chars);
    uint32_t hash = hash_string(chars, len);
    return (IniKey){ .hash = hash, .len = len, .chars = (char *)chars, };
}

static char *parse_key(IniKey *key, char *start)
{
    char *curr = start;

    while (*curr != '=' && *curr != '\0' && !is_whitespace(*curr)) curr++;
    size_t key_len = curr - start;
    curr = skip_whitespace(curr);
    if (*curr == '=') {
        curr++;
    } else return NULL;

    key->chars = (char *)malloc(key_len + 1);
    if (key->chars == NULL) return NULL;
    memcpy(key->chars, start, key_len);
    key->chars[key_len] = '\0';
    key->len = key_len;
    key->hash = hash_string(key->chars, key->len);

    return curr;
}

static char *parse_value(double *value, char *start)
{
    char *curr = start;

    curr = skip_whitespace(curr);
    if (!is_digit(*curr)) {
        return NULL;
    }
    *value = strtod(curr, NULL);

    while (is_digit(*curr) && *curr != '\0') {
        curr++;
        if (*curr == '.') {
            curr++;
            while (is_digit(*curr) && *curr != '\0') curr++;
            break;
        }
    }

    return curr;
}

static char *parse_entry(char *start, IniData *data)
{
    if (data->capacity * LOAD_FACTOR < data->count + 1) {
        if (!grow_entries(data)) return NULL;
    }

    IniKey key = (IniKey){ 0, 0, NULL };
    char *curr = parse_key(&key, start);
    if (curr == NULL) return NULL;

    double value = 0;
    curr = parse_value(&value, curr);
    if (curr == NULL) {
        free(key.chars);
        return NULL;
    }

    // Limit 1 entry per line; Skip to next line
    while (*curr != '\n' && *curr != '\0') {
        if (!is_whitespace(*curr)) {
            free(key.chars);
            return NULL;
        }
        curr++;
    }

    // Insert newly parsed entry into table
    IniEntry *e = find_entry(data->entries, &key, data->capacity);
    e->key = key;
    e->value = value;

    data->count++;
    return curr;
}

static bool parse_file(char *source, IniData *data)
{
    char *curr = source;
    while (*curr != '\0') {
        curr = skip_whitespace(curr);
        if (*curr != '\0') {
            curr = parse_entry(curr, data);
            if (curr == NULL) return false;
        }
    }

    return true;
}

static IniData *struct_init(void)
{
    IniData *data = (IniData *)malloc(sizeof(IniData));
    if (data == NULL) return NULL;

    data->count = 0;
    data->capacity = 0;
    data->entries = NULL;

    return data;
}

IniData *ini_load(const char *file_path)
{
    IniData *data = struct_init();
    if (data == NULL) return NULL;

    if (file_path == NULL) return data;

    char *source = read_file(file_path);
    if (source == NULL) {
        free(data);
        return NULL;
    }
    if (!parse_file(source, data)) {
        free(data);
        free(source);
        return NULL;
    }

    free(source);
    return data;
}

void ini_unload(IniData *data)
{
    if (data == NULL) return;

    if (data->entries != NULL) {
        for (size_t i = 0; i < data->capacity; i++) {
            IniEntry *e = &data->entries[i];
            if (e->key.chars != NULL) free(e->key.chars);
        }
        free(data->entries);
    }

    free(data);
}

bool ini_dump(IniData *data, const char *file_path)
{
    if (data->count == 0) return false;

    FILE *fp = fopen(file_path, "wb");
    if (fp == NULL) return false;

    for (size_t i = 0; i < data->capacity; i++) {
        IniEntry *e = &data->entries[i];
        if (e->key.chars == NULL) continue;

        fprintf(fp, "%s = %g\n", e->key.chars, e->value);
    }

    fclose(fp);
    return true;
}

bool ini_get(IniData *data, const char *key, double *value)
{
    if (data->count == 0) return false;

    IniKey temp_key = make_key(key);
    IniEntry *e = find_entry(data->entries, &temp_key, data->capacity);
    if (e->key.chars == NULL) return false;

    *value = e->value;

    return true;
}

bool ini_set(IniData *data, const char *key, double value)
{
    if (data->count == 0) {
        if (!grow_entries(data)) return false;
    }

    IniKey temp_key = make_key(key);
    IniEntry *e = find_entry(data->entries, &temp_key, data->capacity);

    // Insert new entry if empty, or else update existing entry
    if (e->key.chars == NULL) {
        if (data->capacity * LOAD_FACTOR < data->count + 1) {
            if (!grow_entries(data)) return false;
        }

        temp_key.chars = (char *)malloc(sizeof(char) * temp_key.len);
        memcpy(temp_key.chars, key, temp_key.len);
        e->key = temp_key;
        data->count++;
    }

    e->value = value;

    return true;
}

#endif
#endif
#endif

