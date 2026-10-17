#include "reference_config.h"
#include "json_config.h"
#include "script_engine.h"
#include "browser_scroll_plan.h"
#include <bcrypt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <wctype.h>

static int fail(char *error, size_t cap, const char *message) { snprintf(error, cap, "%s", message); return 0; }
static int text_value(const JsonValue *v) { return v && v->kind == JSON_STRING; }
static int uint_value(const JsonValue *v, unsigned lo, unsigned hi, unsigned *out) {
    if (!v || v->kind != JSON_NUMBER || v->number < lo || v->number > hi) return 0;
    *out = v->number; return 1;
}
static int range(const JsonValue *parent, const wchar_t *key, unsigned lo, unsigned hi, unsigned out[2]) {
    const JsonValue *v = json_get(parent, key), *a;
    if (!v || v->kind != JSON_ARRAY || !(a = v->child) || !a->next || a->next->next) return 0;
    return uint_value(a, lo, hi, out) && uint_value(a->next, lo, hi, out+1) && out[0] <= out[1];
}
int reference_sha256(const wchar_t *text, char hex[65]) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char digest[32];
    char *bytes = NULL;
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, NULL, 0, NULL, NULL), ok = 0;
    unsigned i;
    if (!size || !(bytes = malloc((size_t)size))) return 0;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, bytes, size, NULL, NULL)) goto done;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) < 0 ||
        BCryptHashData(hash, (PUCHAR)bytes, (ULONG)size-1, 0) < 0 ||
        BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) goto done;
    for (i = 0; i < 32; ++i) snprintf(hex+2*i, 3, "%02x", digest[i]);
    ok = 1;
done:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    free(bytes); return ok;
}
wchar_t *reference_normalize(const wchar_t *text) {
    wchar_t *out = calloc(wcslen(text)+1, sizeof(*out)), *write = out;
    if (!out) return NULL;
    while (*text) {
        if (*text == L'\r') { ++text; if (*text == L'\n') ++text; *write++ = L'\n'; }
        else *write++ = *text++;
    }
    return out;
}
int reference_prefix(const wchar_t *source, const wchar_t *saved, size_t *offset) {
    size_t i = 0;
    while (saved[i]) { if (!source[i] || source[i] != saved[i]) return 0; ++i; }
    if (i && saved[i-1] >= 0xd800 && saved[i-1] <= 0xdbff) return 0;
    *offset = i; return 1;
}
static int file_name_valid(const wchar_t *name) {
    const wchar_t *s, *ext = wcsrchr(name, L'.');
    if (!*name || wcslen(name) > 160 || !ext || (wcscmp(ext,L".py") && wcscmp(ext,L".cjs"))) return 0;
    if (name[0] == L'.' || wcsstr(name, L"..")) return 0;
    for (s = name; *s; ++s)
        if (!((*s >= L'A' && *s <= L'Z') || (*s >= L'a' && *s <= L'z') ||
              (*s >= L'0' && *s <= L'9') || *s == L'_' || *s == L'-' || *s == L'.')) return 0;
    /* Windows device basenames are reserved even when they have an extension. */
    if ((ext-name == 3 && (!_wcsnicmp(name,L"CON",3) || !_wcsnicmp(name,L"PRN",3) ||
        !_wcsnicmp(name,L"AUX",3) || !_wcsnicmp(name,L"NUL",3))) ||
        (ext-name == 4 && (!_wcsnicmp(name,L"COM",3) || !_wcsnicmp(name,L"LPT",3)) &&
        name[3] >= L'1' && name[3] <= L'9')) return 0;
    return 1;
}
static int absolute_path(const wchar_t *base, const wchar_t *text, wchar_t out[MAX_PATH], int require_absolute) {
    wchar_t joined[MAX_PATH*2];
    DWORD n;
    int absolute = ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) &&
        text[1] == L':' && (text[2] == L'\\' || text[2] == L'/');
    if (!*text || wcslen(text) >= MAX_PATH || wcspbrk(text, L"\"%\r\n\t") || (require_absolute && !absolute)) return 0;
    if (absolute) wcscpy(joined, text);
    else {
        if (wcschr(text,L':') || text[0] == L'\\' || text[0] == L'/') return 0;
        swprintf(joined, MAX_PATH*2, L"%ls\\%ls", base, text);
    }
    n = GetFullPathNameW(joined, MAX_PATH, out, NULL);
    if (!n || n >= MAX_PATH) return 0;
    for (n = 0; out[n]; ++n) if (out[n] == L'/') out[n] = L'\\';
    n = (DWORD)wcslen(out);
    if (n > 3 && out[n-1] == L'\\') out[n-1] = 0;
    return 1;
}
static int docs_url(const wchar_t *url);
static int load_docs_topics(ReferenceConfig *c, const JsonValue *topics, int required, const char **problem) {
    static const wchar_t *const topic_keys[] = {L"query",L"url"};
    const JsonValue *v;
    size_t i, count = 0;
    if (!topics) return !required;
    if (topics->kind != JSON_ARRAY) return 0;
    for (v = topics->child; v; v = v->next) if (++count > 64) return 0;
    if (!count) return !required;
    if (!(c->topics = calloc(count,sizeof(*c->topics)))) return 0;
    for (v = topics->child, i = 0; v; v = v->next, ++i) {
        const JsonValue *query = json_get(v,L"query"), *url = json_get(v,L"url");
        if (!json_keys(v,topic_keys,2) || !text_value(query) || !text_value(url) ||
            !*query->string || wcslen(query->string) > 150 || wcspbrk(query->string,L"\r\n\t") || !docs_url(url->string)) {
            *problem = "each browser topic needs a short search query and an official Python or MDN JavaScript URL"; return 0;
        }
        c->topics[i].query = _wcsdup(query->string); c->topics[i].url = _wcsdup(url->string);
        c->topics[i].category = !wcsncmp(url->string,L"https://docs.python.org/",24) ? REFERENCE_PYTHON : REFERENCE_JAVASCRIPT;
        c->topic_count = i+1;
        if (!c->topics[i].query || !c->topics[i].url) return 0;
    }
    return 1;
}
static int docs_url(const wchar_t *url) {
    return wcslen(url) < 220 && !wcspbrk(url,L" \"\\\r\n\t") &&
        (!wcsncmp(url,L"https://docs.python.org/3/",wcslen(L"https://docs.python.org/3/")) ||
         !wcsncmp(url,L"https://developer.mozilla.org/en-US/docs/Web/JavaScript/",
                    wcslen(L"https://developer.mozilla.org/en-US/docs/Web/JavaScript/")));
}
static int load_url_list(ReferenceConfig *c, char *error, size_t cap) {
    wchar_t *text = NULL, *at;
    size_t found = 0;
    if (!read_utf8(c->url_list_path,&text,error,cap)) return 0;
    for (at = text; *at;) {
        wchar_t url[220];
        const wchar_t *end;
        size_t length, i;
        ReferenceTopic *grown;
        if (_wcsnicmp(at,L"https://",8) && _wcsnicmp(at,L"http://",7)) { ++at; continue; }
        for (end = at; *end && !iswspace(*end); ++end) {}
        length = (size_t)(end-at);
        if (length >= sizeof(url)/sizeof(*url)) {
            free(text); return fail(error,cap,"URL list contains a URL longer than 219 characters");
        }
        wmemcpy(url,at,length); url[length] = 0; at = (wchar_t *)end;
        if (!browser_url_valid(url)) {
            free(text); return fail(error,cap,"URL list contains an invalid HTTP/HTTPS URL");
        }
        ++found;
        for (i = 0; i < c->topic_count; ++i) if (!wcscmp(c->topics[i].url,url)) break;
        if (i < c->topic_count) continue;
        if (c->topic_count >= 512) {
            free(text); return fail(error,cap,"browser choices exceed 512 unique URLs");
        }
        grown = realloc(c->topics,(c->topic_count+1)*sizeof(*grown));
        if (!grown) { free(text); return fail(error,cap,"out of memory loading URL list"); }
        c->topics = grown;
        memset(c->topics+c->topic_count,0,sizeof(*c->topics));
        c->topics[c->topic_count].url = _wcsdup(url);
        if (!c->topics[c->topic_count].url) { free(text); return fail(error,cap,"out of memory loading URL list"); }
        c->topics[c->topic_count++].category = REFERENCE_URL;
    }
    free(text);
    return found ? 1 : fail(error,cap,"URL list contains no HTTP/HTTPS URLs");
}
/* Adds a research search once; repeated queries (ignoring case) are skipped. */
static int add_research_query(ReferenceConfig *c, const wchar_t *query, char *error, size_t cap) {
    ReferenceTopic *grown;
    size_t i;
    for (i = 0; i < c->topic_count; ++i)
        if (c->topics[i].category == REFERENCE_RESEARCH && !_wcsicmp(c->topics[i].query,query)) return 1;
    if (c->topic_count >= 1024) return fail(error,cap,"browser choices exceed 1024 entries");
    grown = realloc(c->topics,(c->topic_count+1)*sizeof(*grown));
    if (!grown) return fail(error,cap,"out of memory loading research queries");
    c->topics = grown;
    memset(c->topics+c->topic_count,0,sizeof(*c->topics));
    c->topics[c->topic_count].query = _wcsdup(query);
    c->topics[c->topic_count].url = _wcsdup(L"https://www.google.com/?hl=en");
    c->topics[c->topic_count].category = REFERENCE_RESEARCH;
    ++c->topic_count;
    if (!c->topics[c->topic_count-1].query || !c->topics[c->topic_count-1].url)
        return fail(error,cap,"out of memory loading research queries");
    return 1;
}
/* One Google search query per line; blank lines and # comments are ignored. */
static int load_research_list(ReferenceConfig *c, char *error, size_t cap) {
    wchar_t *text = NULL, *line, *next;
    size_t found = 0;
    if (!read_utf8(c->research_path,&text,error,cap)) return 0;
    for (line = text; line && *line; line = next) {
        wchar_t *end;
        next = wcschr(line,L'\n');
        if (next) *next++ = 0;
        while (iswspace(*line)) ++line;
        end = line + wcslen(line);
        while (end > line && iswspace(end[-1])) *--end = 0;
        if (!*line || *line == L'#') continue;
        if (end-line > 150 || wcschr(line,L'\t')) {
            free(text); return fail(error,cap,"research list contains a query longer than 150 characters or a tab");
        }
        ++found;
        if (!add_research_query(c,line,error,cap)) { free(text); return 0; }
    }
    free(text);
    return found ? 1 : fail(error,cap,"research list contains no search queries");
}
const char *reference_category_name(ReferenceCategory category) {
    switch (category) {
        case REFERENCE_PYTHON: return "Python documentation";
        case REFERENCE_JAVASCRIPT: return "JavaScript documentation";
        case REFERENCE_URL: return "URL list page";
        case REFERENCE_RESEARCH: return "vehicle business research";
        default: return "unknown";
    }
}
size_t reference_pick_topic(const ReferenceConfig *c, CodingSchedule *schedule, size_t previous) {
    unsigned counts[REFERENCE_CATEGORY_COUNT] = {0}, groups[REFERENCE_CATEGORY_COUNT], available = 0, pick;
    size_t i;
    unsigned category;
    if (!c || !schedule || !c->topics || !c->topic_count) return SIZE_MAX;
    for (i = 0; i < c->topic_count; ++i) {
        if ((unsigned)c->topics[i].category >= REFERENCE_CATEGORY_COUNT) return SIZE_MAX;
        if (i != previous || c->topic_count == 1) ++counts[c->topics[i].category];
    }
    { unsigned total = 0, any = 0, roll;
      for (category = 0; category < REFERENCE_CATEGORY_COUNT; ++category) any |= c->weights[category];
      for (category = 0; category < REFERENCE_CATEGORY_COUNT; ++category)
          if (counts[category] && (!any || c->weights[category])) {
              groups[available++] = category; total += any ? c->weights[category] : 1;
          }
      if (!available) return SIZE_MAX;
      roll = coding_schedule_pick(schedule,0,total-1);
      for (pick = 0; pick < available; ++pick) {
          unsigned weight = any ? c->weights[groups[pick]] : 1;
          if (roll < weight) break;
          roll -= weight;
      }
      category = groups[pick];
    }
    pick = coding_schedule_pick(schedule,0,counts[category]-1);
    for (i = 0; i < c->topic_count; ++i) {
        if ((i == previous && c->topic_count != 1) || (unsigned)c->topics[i].category != category) continue;
        if (!pick) return i;
        --pick;
    }
    return SIZE_MAX;
}
void reference_config_free(ReferenceConfig *c) {
    size_t i;
    for (i = 0; c->files && i < c->file_count; ++i) {
        free(c->files[i].name); free(c->files[i].source); free(c->files[i].content); free(c->files[i].normalized);
    }
    for (i = 0; c->topics && i < c->topic_count; ++i) { free(c->topics[i].query); free(c->topics[i].url); }
    free(c->files); free(c->topics); memset(c, 0, sizeof(*c));
}
/* ---- Version 2 job files: short, hand-edited settings; sources are read from a folder. ---- */
static int wildcard(const wchar_t *pattern, const wchar_t *name) {
    if (!*pattern) return !*name;
    if (*pattern == L'*') return wildcard(pattern+1,name) || (*name && wildcard(pattern,name+1));
    if (!*name) return 0;
    return (*pattern == L'?' || towlower(*pattern) == towlower(*name)) && wildcard(pattern+1,name+1);
}
static int name_matches(const JsonValue *patterns, const wchar_t *name, int empty_result) {
    const JsonValue *v;
    if (!patterns || !patterns->child) return empty_result;
    for (v = patterns->child; v; v = v->next) if (wildcard(v->string,name)) return 1;
    return 0;
}
static int string_array(const JsonValue *v, size_t max_items, size_t max_length) {
    size_t count = 0;
    if (!v) return 1;
    if (v->kind != JSON_ARRAY) return 0;
    for (v = v->child; v; v = v->next)
        if (++count > max_items || !text_value(v) || !*v->string || wcslen(v->string) > max_length ||
            wcspbrk(v->string,L"\r\n\t")) return 0;
    return 1;
}
static int compare_names(const void *a, const void *b) {
    return _wcsicmp(*(wchar_t *const *)a,*(wchar_t *const *)b);
}
static int add_job_file(ReferenceConfig *c, const wchar_t *folder, const wchar_t *name, char *error, size_t cap) {
    wchar_t path[MAX_PATH], *content = NULL;
    ReferenceFile *f = c->files + c->file_count;
    char label[200];
    size_t j;
    WideCharToMultiByte(CP_UTF8,0,name,-1,label,sizeof(label),NULL,NULL);
    if (!file_name_valid(name) || wcslen(folder)+wcslen(name)+2 >= MAX_PATH) {
        snprintf(error,cap,"%s: only .py/.cjs names with letters, digits, '_', '-', '.' are supported",label); return 0;
    }
    for (j = 0; j < c->file_count; ++j) if (!_wcsicmp(c->files[j].name,name)) {
        snprintf(error,cap,"%s is listed twice",label); return 0;
    }
    swprintf(path,MAX_PATH,L"%ls\\%ls",folder,name);
    if (!read_utf8(path,&content,error,cap)) {
        char detail[300]; snprintf(detail,sizeof(detail),"%s: %s",label,error); snprintf(error,cap,"%s",detail); return 0;
    }
    for (j = 0; content[j]; ++j) if (content[j] < 32 && content[j] != L'\t' && content[j] != L'\r' && content[j] != L'\n') {
        free(content); snprintf(error,cap,"%s contains an unsupported control character",label); return 0;
    }
    memset(f,0,sizeof(*f));
    f->name = _wcsdup(name); f->source = _wcsdup(path); f->content = content;
    f->normalized = reference_normalize(content); f->enabled = 1;
    ++c->file_count;
    if (!f->name || !f->source || !f->normalized) return fail(error,cap,"out of memory reading sources");
    if (!reference_sha256(content,f->sha256)) { snprintf(error,cap,"%s is not valid UTF-8 text",label); return 0; }
    return 1;
}
static int load_job_files(ReferenceConfig *c, const wchar_t *base, const JsonValue *files,
                          const char **problem, char *error, size_t cap) {
    static const wchar_t *const keys[] = {L"folder",L"include",L"exclude",L"list",L"order",L"shuffle_seed"};
    const JsonValue *folder = json_get(files,L"folder"), *include = json_get(files,L"include"),
        *exclude = json_get(files,L"exclude"), *list = json_get(files,L"list"), *order = json_get(files,L"order"), *v;
    wchar_t **names = NULL;
    size_t count = 0, capacity = 0, i;
    unsigned shuffle_seed = 0;
    int ok = 0, shuffle;
    *problem = "files needs folder, optional include/exclude patterns or list, and order alphabetical, listed, or shuffle";
    if (!json_keys(files,keys,6) || !text_value(folder) || !absolute_path(base,folder->string,c->source_folder,0) ||
        !string_array(include,64,100) || !string_array(exclude,256,160) || !string_array(list,1000,160) ||
        !text_value(order) || (wcscmp(order->string,L"alphabetical") && wcscmp(order->string,L"listed") &&
        wcscmp(order->string,L"shuffle")) || (!list && !wcscmp(order->string,L"listed")) ||
        ((v = json_get(files,L"shuffle_seed")) && !uint_value(v,0,2147483647,&shuffle_seed))) return 0;
    shuffle = !wcscmp(order->string,L"shuffle");
    *problem = NULL;
    if (list) {
        for (v = list->child; v; v = v->next) ++capacity;
        if (!(names = calloc(capacity ? capacity : 1,sizeof(*names)))) return fail(error,cap,"out of memory");
        for (v = list->child; v; v = v->next)
            if (!name_matches(exclude,v->string,0)) names[count++] = v->string;
    } else {
        WIN32_FIND_DATAW found;
        wchar_t pattern[MAX_PATH];
        HANDLE search;
        swprintf(pattern,MAX_PATH,L"%ls\\*",c->source_folder);
        search = FindFirstFileW(pattern,&found);
        if (search == INVALID_HANDLE_VALUE) return fail(error,cap,"files folder cannot be read");
        do {
            if (found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_HIDDEN)) continue;
            if (!name_matches(include,found.cFileName,1) || name_matches(exclude,found.cFileName,0)) continue;
            /* Only Python and CommonJS sources are typed; other files in the folder are ignored. */
            if (!file_name_valid(found.cFileName)) continue;
            if (count == capacity) {
                wchar_t **grown = realloc(names,(capacity = capacity ? capacity*2 : 64)*sizeof(*names));
                if (!grown) { FindClose(search); goto done_fail_memory; }
                names = grown;
            }
            if (!(names[count] = _wcsdup(found.cFileName))) { FindClose(search); goto done_fail_memory; }
            ++count;
        } while (FindNextFileW(search,&found));
        FindClose(search);
        if (count > 1000) { fail(error,cap,"more than 1000 matching source files"); goto done; }
    }
    if (!list || !wcscmp(order->string,L"alphabetical")) qsort(names,count,sizeof(*names),compare_names);
    if (!count) { fail(error,cap,"no .py or .cjs source files match the files settings"); goto done; }
    if (shuffle) {
        /* A fixed seed keeps the same order on every resume; 0 derives one from the destination. */
        uint32_t state = shuffle_seed;
        if (!state) {
            char hex[65];
            if (!reference_sha256(c->destination,hex)) { fail(error,cap,"cannot derive shuffle seed"); goto done; }
            state = (uint32_t)strtoul((char[9]){hex[0],hex[1],hex[2],hex[3],hex[4],hex[5],hex[6],hex[7],0},NULL,16) | 1u;
        }
        for (i = count; i > 1; --i) {
            size_t j; wchar_t *swap;
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            j = state % i;
            swap = names[i-1]; names[i-1] = names[j]; names[j] = swap;
        }
    }
    if (!(c->files = calloc(count,sizeof(*c->files)))) goto done_fail_memory;
    for (i = 0; i < count; ++i) if (!add_job_file(c,c->source_folder,names[i],error,cap)) goto done;
    ok = 1;
    goto done;
done_fail_memory:
    fail(error,cap,"out of memory listing source files");
done:
    if (!list) for (i = 0; i < count; ++i) free(names[i]);
    free(names);
    return ok;
}
typedef struct { const wchar_t *name, *value; size_t base_length; } TemplateSlot;
static size_t template_base(const wchar_t *name, size_t length) {
    while (length > 1 && name[length-1] >= L'0' && name[length-1] <= L'9') --length;
    return length;
}
/* Expand "{brand} campervan hire {city}" over every value; "{brand2}" draws a different brand. */
static int expand_template(ReferenceConfig *c, const JsonValue *values, const wchar_t *done, const wchar_t *rest,
                           TemplateSlot *slots, unsigned used, unsigned *generated, char *error, size_t cap) {
    const wchar_t *open = wcschr(rest,L'{'), *close;
    wchar_t text[400], key[32];
    const JsonValue *list, *v;
    size_t length, base;
    unsigned i;
    if (!open) {
        if (wcslen(done)+wcslen(rest) > 150) return fail(error,cap,"a research template produces a query over 150 characters");
        swprintf(text,400,L"%ls%ls",done,rest);
        if (++*generated > 400) return fail(error,cap,"research templates produce more than 400 queries");
        return add_research_query(c,text,error,cap);
    }
    close = wcschr(open,L'}');
    length = close ? (size_t)(close-open-1) : 0;
    if (!close || !length || length >= 32 || used >= 8) return fail(error,cap,"research template has a malformed {placeholder}");
    wmemcpy(key,open+1,length); key[length] = 0;
    if (wcslen(done)+(size_t)(open-rest) >= 300) return fail(error,cap,"research template is too long");
    for (i = 0; i < used; ++i) if (!wcscmp(slots[i].name,key)) {
        swprintf(text,400,L"%ls%.*ls%ls",done,(int)(open-rest),rest,slots[i].value);
        return expand_template(c,values,text,close+1,slots,used,generated,error,cap);
    }
    base = length;
    list = json_get(values,key);
    if (!list) {
        wchar_t stem[32];
        base = template_base(key,length);
        wmemcpy(stem,key,base); stem[base] = 0;
        list = base < length ? json_get(values,stem) : NULL;
    }
    if (!list) {
        char name[64]; WideCharToMultiByte(CP_UTF8,0,key,-1,name,sizeof(name),NULL,NULL);
        snprintf(error,cap,"research template placeholder {%s} has no values list",name); return 0;
    }
    for (v = list->child; v; v = v->next) {
        int repeated = 0;
        for (i = 0; i < used; ++i)
            if (slots[i].base_length == base && !wcsncmp(slots[i].name,key,base) && !wcscmp(slots[i].value,v->string))
                repeated = 1;
        if (repeated) continue;
        swprintf(text,400,L"%ls%.*ls%ls",done,(int)(open-rest),rest,v->string);
        slots[used].name = key; slots[used].value = v->string; slots[used].base_length = base;
        if (!expand_template(c,values,text,close+1,slots,used+1,generated,error,cap)) return 0;
    }
    return 1;
}
static int load_job_browser(ReferenceConfig *c, const wchar_t *base, const JsonValue *browser,
                            const char **problem, char *error, size_t cap) {
    static const wchar_t *const weight_keys[] = {L"research",L"url_list",L"python_docs",L"js_docs"};
    static const ReferenceCategory weight_categories[] = {REFERENCE_RESEARCH,REFERENCE_URL,REFERENCE_PYTHON,REFERENCE_JAVASCRIPT};
    static const wchar_t *const research_keys[] = {L"queries_file",L"results_to_open",L"templates"};
    static const wchar_t *const template_keys[] = {L"patterns",L"values"};
    const JsonValue *weights = json_get(browser,L"weights"), *research = json_get(browser,L"research"), *v;
    unsigned i, total = 0;
    if (weights) {
        if (!json_keys(weights,weight_keys,4)) { *problem = "browser weights allow research, url_list, python_docs, js_docs"; return 0; }
        for (i = 0; i < 4; ++i) {
            v = json_get(weights,weight_keys[i]);
            if (v && !uint_value(v,0,1000,&c->weights[weight_categories[i]])) {
                *problem = "browser weights must be whole numbers from 0 to 1000"; return 0;
            }
            total += c->weights[weight_categories[i]];
        }
        if (!total) { *problem = "at least one browser weight must be above 0"; return 0; }
    }
    if (research) {
        const JsonValue *templates = json_get(research,L"templates");
        if (!json_keys(research,research_keys,3)) { *problem = "browser research allows queries_file, results_to_open, templates"; return 0; }
        if (json_get(research,L"results_to_open") && !range(research,L"results_to_open",1,3,c->results_to_open)) {
            *problem = "research results_to_open must be a range from 1 to 3, e.g. [1, 2]"; return 0;
        }
        v = json_get(research,L"queries_file");
        if (v) {
            if (!text_value(v) || !absolute_path(base,v->string,c->research_path,0)) {
                *problem = "research queries_file must be a valid text-file path"; return 0;
            }
            *problem = NULL;
            if (!load_research_list(c,error,cap)) return 0;
        }
        if (templates) {
            const JsonValue *patterns = json_get(templates,L"patterns"), *values = json_get(templates,L"values");
            TemplateSlot slots[8];
            unsigned generated = 0;
            if (!json_keys(templates,template_keys,2) || !patterns || !patterns->child || !string_array(patterns,50,150) ||
                !values || values->kind != JSON_OBJECT) {
                *problem = "research templates need a patterns list and a values object"; return 0;
            }
            for (v = values->child; v; v = v->next)
                if (!v->child || !string_array(v,200,60)) { *problem = "each template values entry must be a list of short text"; return 0; }
            *problem = NULL;
            for (v = patterns->child; v; v = v->next)
                if (!expand_template(c,values,L"",v->string,slots,0,&generated,error,cap)) return 0;
        }
    }
    return 1;
}
static int load_auto_restart(ReferenceConfig *c, const JsonValue *restart) {
    static const wchar_t *const keys[] = {L"enabled",L"max_restarts",L"wait_seconds"};
    const JsonValue *v;
    if (!restart) return 1;
    v = json_get(restart,L"enabled");
    if (!json_keys(restart,keys,3) || !v || v->kind != JSON_BOOL ||
        !uint_value(json_get(restart,L"max_restarts"),0,100,&c->max_restarts) ||
        !uint_value(json_get(restart,L"wait_seconds"),10,3600,&c->restart_wait_seconds)) return 0;
    c->auto_restart = (int)v->number;
    return 1;
}
int reference_config_load(const wchar_t *path, ReferenceConfig *c, char *error, size_t cap) {
    static const wchar_t *const root_keys[] = {L"version",L"destination",L"progress_file",L"countdown_seconds",
        L"seed",L"line_endings",L"typing",L"actions",L"browser",L"files",L"auto_restart"};
    static const wchar_t *const typing_keys[] = {L"character_ms",L"newline_ms",L"pause_every_chars",L"pause_ms"};
    static const wchar_t *const action_keys[] = {L"pause_ms",L"mouse_move_ms"};
    static const wchar_t *const browser_keys[] = {L"enabled",L"coding_interval_seconds",L"visit_seconds",L"topics",L"url_list_file",
        L"research_file",L"launch"};
    static const wchar_t *const job_browser_keys[] = {L"enabled",L"coding_interval_seconds",L"visit_seconds",L"launch",
        L"weights",L"docs_topics",L"url_list_file",L"research"};
    static const wchar_t *const file_keys[] = {L"enabled",L"source",L"name",L"sha256",L"content"};
    wchar_t *text = NULL, base[MAX_PATH], *last;
    JsonValue *root = NULL;
    const JsonValue *v, *typing, *actions, *browser, *files, *topics;
    size_t i, enabled = 0;
    unsigned version, seed;
    const char *problem = "invalid reference config schema or unknown field";
    memset(c, 0, sizeof(*c));
    if (!read_utf8(path, &text, error, cap)) return 0;
    if (!json_parse(text, &root, error, cap)) { free(text); return 0; }
    free(text);
    if (!GetFullPathNameW(path,MAX_PATH,base,NULL) || !(last = wcsrchr(base,L'\\'))) goto fail;
    *last = 0;
    if (!uint_value(json_get(root,L"version"),1,2,&version) ||
        !json_keys(root,root_keys,sizeof(root_keys)/sizeof(*root_keys) - (version == 1)) ||
        !uint_value(json_get(root,L"countdown_seconds"),1,3600,&c->countdown_seconds) ||
        !uint_value(json_get(root,L"seed"),0,2147483647,&seed)) goto fail;
    c->seed = seed;
    v = json_get(root,L"line_endings");
    if ((version == 1 || v) && (!text_value(v) || wcscmp(v->string,L"normalize_lf"))) goto fail;
    v = json_get(root,L"destination");
    if (!text_value(v) || !absolute_path(base,v->string,c->destination,1) || wcslen(c->destination) > 90) {
        problem = "destination must be an absolute project folder path of at most 90 characters"; goto fail;
    }
    v = json_get(root,L"progress_file");
    if (!text_value(v) || !absolute_path(base,v->string,c->progress_path,0) ||
        wcslen(c->progress_path) > MAX_PATH-6 ||
        !wcsrchr(c->progress_path,L'.') || _wcsicmp(wcsrchr(c->progress_path,L'.'),L".state")) goto fail;
    typing = json_get(root,L"typing"); actions = json_get(root,L"actions"); browser = json_get(root,L"browser");
    if (!json_keys(typing,typing_keys,4) || !json_keys(actions,action_keys,2) || (version == 1 ? !json_keys(browser,browser_keys,7) : !json_keys(browser,job_browser_keys,8)) ||
        !range(typing,L"character_ms",10,5000,c->char_ms) || !range(typing,L"newline_ms",10,30000,c->newline_ms) ||
        !range(typing,L"pause_every_chars",1,100000,c->pause_every_chars) || !range(typing,L"pause_ms",1,600000,c->typing_pause_ms) ||
        !range(actions,L"pause_ms",1,30000,c->action_pause_ms) || !range(actions,L"mouse_move_ms",100,30000,c->mouse_move_ms) ||
        !range(browser,L"coding_interval_seconds",1,3600,c->interval_seconds) ||
        !range(browser,L"visit_seconds",1,300,c->visit_seconds)) goto fail;
    v = json_get(browser,L"enabled");
    if (!v || v->kind != JSON_BOOL) goto fail;
    c->browser_enabled = (int)v->number;
    topics = json_get(browser,version == 1 ? L"topics" : L"docs_topics");
    if (!load_docs_topics(c,topics,version == 1,&problem)) goto fail;
    v = json_get(browser,L"url_list_file");
    if (v) {
        if (!text_value(v) || !absolute_path(base,v->string,c->url_list_path,0)) {
            problem = "browser url_list_file must be a valid text-file path"; goto fail;
        }
        if (!load_url_list(c,error,cap)) goto failed_message;
    }
    v = json_get(browser,L"launch");
    if (v) {
        if (!text_value(v) || (wcscmp(v->string,L"run") && wcscmp(v->string,L"taskbar"))) {
            problem = "browser launch must be \"run\" or \"taskbar\""; goto fail;
        }
        c->browser_launch = !wcscmp(v->string,L"taskbar") ? REFERENCE_LAUNCH_TASKBAR : REFERENCE_LAUNCH_RUN;
    }
    c->results_to_open[0] = c->results_to_open[1] = 1;
    if (version == 2) {
        if (!load_job_browser(c,base,browser,&problem,error,cap)) { if (problem) goto fail; goto failed_message; }
        if (!load_auto_restart(c,json_get(root,L"auto_restart"))) {
            problem = "auto_restart needs enabled, max_restarts 0-100, and wait_seconds 10-3600"; goto fail;
        }
        if (c->browser_enabled) {
            size_t t; int usable = 0, any = 0;
            for (t = 0; t < REFERENCE_CATEGORY_COUNT; ++t) any |= c->weights[t] != 0;
            for (t = 0; t < c->topic_count; ++t) usable |= !any || c->weights[c->topics[t].category];
            if (!usable) { problem = "no browser choices: add queries, URLs, or docs topics for a category with weight above 0"; goto fail; }
        }
        if (!load_job_files(c,base,json_get(root,L"files"),&problem,error,cap)) { if (problem) goto fail; goto failed_message; }
        json_free(root); return 1;
    }
    v = json_get(browser,L"research_file");
    if (v) {
        if (!text_value(v) || !absolute_path(base,v->string,c->research_path,0)) {
            problem = "browser research_file must be a valid text-file path"; goto fail;
        }
        if (!load_research_list(c,error,cap)) goto failed_message;
    }
    files = json_get(root,L"files");
    if (!files || files->kind != JSON_ARRAY) goto fail;
    for (v = files->child; v; v = v->next) if (++c->file_count > 1000) goto fail;
    if (!c->file_count || !(c->files = calloc(c->file_count,sizeof(*c->files)))) goto fail;
    for (v = files->child, i = 0; v; v = v->next, ++i) {
        const JsonValue *name = json_get(v,L"name"), *source = json_get(v,L"source"),
            *content = json_get(v,L"content"), *hash = json_get(v,L"sha256"), *on = json_get(v,L"enabled");
        char actual[65], provided[65]; size_t j;
        ReferenceFile *f = c->files+i;
        if (!json_keys(v,file_keys,5) || !text_value(name) || !file_name_valid(name->string) ||
            !text_value(source) || !text_value(content) || !text_value(hash) || wcslen(hash->string) != 64 ||
            !on || on->kind != JSON_BOOL) { problem = "invalid file entry, filename, enabled flag, or SHA-256"; goto fail; }
        for (j = 0; j < i; ++j) if (!_wcsicmp(c->files[j].name,name->string)) {
            problem = "duplicate output filename (Windows is case insensitive)"; goto fail;
        }
        for (j = 0; content->string[j]; ++j) if (content->string[j] < 32 &&
            content->string[j] != L'\t' && content->string[j] != L'\r' && content->string[j] != L'\n') {
            problem = "source contains an unsupported control character"; goto fail;
        }
        for (j = 0; j < 64; ++j) {
            wchar_t ch = hash->string[j];
            if (!((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f'))) goto fail;
            provided[j] = (char)ch;
        }
        provided[64] = 0;
        if (!reference_sha256(content->string,actual) || strcmp(actual,provided)) {
            snprintf(error,cap,"file %zu: embedded content does not match its SHA-256",i+1); goto failed_message;
        }
        f->name = _wcsdup(name->string); f->source = _wcsdup(source->string); f->content = _wcsdup(content->string);
        f->normalized = reference_normalize(content->string); memcpy(f->sha256,actual,65); f->enabled = (int)on->number;
        enabled += f->enabled != 0;
        if (!f->name || !f->source || !f->content || !f->normalized) goto fail;
    }
    if (!enabled) { problem = "enable at least one reference file"; goto fail; }
    json_free(root); return 1;
fail:
    fail(error,cap,problem);
failed_message:
    json_free(root); reference_config_free(c); return 0;
}

static int progress_load(ReferenceConfig *c, char *error, size_t cap) {
    FILE *file;
    char line[512], identity[65], expected[65];
    DWORD attrs = GetFileAttributesW(c->progress_path);
    size_t i;
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return 1;
    }
    if (attrs == INVALID_FILE_ATTRIBUTES || attrs & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))
        return fail(error,cap,"progress path is not a readable normal file");
    file = _wfopen(c->progress_path,L"rb");
    if (!file) return fail(error,cap,"cannot read progress file");
    if (!reference_sha256(c->destination,expected) || !fgets(line,sizeof(line),file) ||
        strcmp(line,"TUTORIAL_REFERENCE_PROGRESS 1\n") || !fgets(identity,sizeof(identity),file) ||
        strcmp(identity,expected) || fgetc(file) != '\n') {
        fclose(file); return fail(error,cap,"progress file is invalid or belongs to a different destination");
    }
    while (fgets(line,sizeof(line),file)) {
        char name[161], hash[65], extra;
        if (sscanf(line,"%64[0-9a-f]\t%160[A-Za-z0-9_.-]%c",hash,name,&extra) != 3 ||
            strlen(hash) != 64 || extra != '\n') { fclose(file); return fail(error,cap,"invalid progress entry"); }
        for (i = 0; i < c->file_count; ++i) {
            char candidate[161];
            WideCharToMultiByte(CP_UTF8,0,c->files[i].name,-1,candidate,sizeof(candidate),NULL,NULL);
            if (!strcmp(candidate,name) && !strcmp(c->files[i].sha256,hash)) c->files[i].owned = 1;
        }
    }
    if (ferror(file)) { fclose(file); return fail(error,cap,"progress read failed"); }
    fclose(file); return 1;
}
int reference_config_preflight(ReferenceConfig *c, char *error, size_t cap) {
    size_t i;
    DWORD attrs = GetFileAttributesW(c->destination);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) || attrs & FILE_ATTRIBUTE_REPARSE_POINT)
        return fail(error,cap,"destination must already exist and be a normal directory");
    for (i = 0; i < c->file_count; ++i) { c->files[i].owned = c->files[i].done = 0; c->files[i].offset = 0; }
    if (!progress_load(c,error,cap)) return 0;
    for (i = 0; i < c->file_count; ++i) {
        wchar_t output[MAX_PATH], *saved = NULL, *normalized;
        ReferenceFile *f = c->files+i;
        if (!f->enabled) continue;
        swprintf(output,MAX_PATH,L"%ls\\%ls",c->destination,f->name);
        attrs = GetFileAttributesW(output);
        if (attrs == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) continue;
        if (attrs == INVALID_FILE_ATTRIBUTES || attrs & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT) ||
            !read_utf8(output,&saved,error,cap)) return fail(error,cap,"an output path is not a readable normal UTF-8 file");
        normalized = reference_normalize(saved); free(saved);
        if (!normalized) return fail(error,cap,"out of memory");
        if (!wcscmp(f->normalized,normalized)) { f->done = 1; f->offset = wcslen(f->normalized); }
        else if (!f->owned) {
            char name[200]; WideCharToMultiByte(CP_UTF8,0,f->name,-1,name,sizeof(name),NULL,NULL);
            free(normalized); snprintf(error,cap,"%s contains unrecorded partial or different content; no files were changed",name); return 0;
        } else if (!reference_prefix(f->normalized,normalized,&f->offset)) {
            char name[200]; size_t mismatch = 0;
            WideCharToMultiByte(CP_UTF8,0,f->name,-1,name,sizeof(name),NULL,NULL);
            while (f->normalized[mismatch] && normalized[mismatch] && f->normalized[mismatch] == normalized[mismatch]) ++mismatch;
            snprintf(error,cap,"%s: saved text differs at character %zu (expected U+%04x, saved U+%04x); original preserved",
                name,mismatch,(unsigned)f->normalized[mismatch],(unsigned)normalized[mismatch]);
            free(normalized); return 0;
        }
        free(normalized);
    }
    /* Finish a partly typed file before starting new ones, keeping the configured order otherwise. */
    { size_t write = 0, j;
      for (i = 0; i < c->file_count; ++i)
          if (c->files[i].enabled && c->files[i].owned && !c->files[i].done && c->files[i].offset) {
              ReferenceFile partial = c->files[i];
              for (j = i; j > write; --j) c->files[j] = c->files[j-1];
              c->files[write++] = partial;
          }
    }
    return 1;
}
int reference_config_claim(ReferenceConfig *c, size_t index, char *error, size_t cap) {
    wchar_t temporary[MAX_PATH];
    FILE *file;
    char identity[65]; size_t i;
    c->files[index].owned = 1;
    swprintf(temporary,MAX_PATH,L"%ls.tmp",c->progress_path);
    if (!reference_sha256(c->destination,identity)) return fail(error,cap,"cannot hash progress destination");
    file = _wfopen(temporary,L"wb");
    if (!file) return fail(error,cap,"cannot write progress file; ensure its parent folder exists");
    fprintf(file,"TUTORIAL_REFERENCE_PROGRESS 1\n%s\n",identity);
    for (i = 0; i < c->file_count; ++i) if (c->files[i].owned) {
        char name[161]; WideCharToMultiByte(CP_UTF8,0,c->files[i].name,-1,name,sizeof(name),NULL,NULL);
        fprintf(file,"%s\t%s\n",c->files[i].sha256,name);
    }
    { int failed = ferror(file); if (fclose(file)) failed = 1;
      if (failed) { DeleteFileW(temporary); return fail(error,cap,"progress write failed"); } }
    if (!MoveFileExW(temporary,c->progress_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary); return fail(error,cap,"cannot commit progress file");
    }
    return 1;
}
