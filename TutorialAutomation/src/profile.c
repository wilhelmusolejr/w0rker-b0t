#include "profile.h"
#include "json_config.h"
#include "script_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

static const wchar_t *const task_names[PROFILE_TASK_COUNT] = {
    L"programming", L"browser", L"excel", L"notepad", L"image_viewer", L"cad", L"files"
};
static const wchar_t *const gender_names[PROFILE_GENDER_COUNT] = { L"male", L"female" };

static int fail(char *error, size_t cap, const char *message) { snprintf(error, cap, "%s", message); return 0; }
static int fail_at(char *error, size_t cap, size_t index, const char *message) {
    snprintf(error, cap, "profile %zu: %s", index+1, message); return 0;
}
static int lookup(const JsonValue *v, const wchar_t *const *names, int count) {
    int i;
    if (!v || v->kind != JSON_STRING) return -1;
    for (i = 0; i < count; ++i) if (!wcscmp(v->string, names[i])) return i;
    return -1;
}
static int plain_text(const JsonValue *v, size_t max) {
    return v && v->kind == JSON_STRING && *v->string && wcslen(v->string) <= max && !wcspbrk(v->string, L"\r\n\t");
}
/* Letters, digits, hyphen and underscore; used for local IDs. */
static int token(const wchar_t *s, size_t max) {
    size_t n = 0;
    for (; *s; ++s, ++n)
        if (!((*s >= L'A' && *s <= L'Z') || (*s >= L'a' && *s <= L'z') ||
              (*s >= L'0' && *s <= L'9') || *s == L'-' || *s == L'_')) return 0;
    return n && n <= max;
}
/* Windows computer names: 1-15 letters, digits or hyphens, not all digits, no leading or trailing hyphen. */
static int computer_name(const wchar_t *s) {
    size_t n = wcslen(s), i;
    int letter = 0;
    if (!n || n > 15 || s[0] == L'-' || s[n-1] == L'-') return 0;
    for (i = 0; i < n; ++i) {
        if ((s[i] >= L'A' && s[i] <= L'Z') || (s[i] >= L'a' && s[i] <= L'z') || s[i] == L'-') letter = 1;
        else if (!(s[i] >= L'0' && s[i] <= L'9')) return 0;
    }
    return letter;
}
/* "Desktop", "Documents", "Desktop\Projects", or "C:\Work": no "..", no characters Windows forbids. */
static int project_path_valid(const wchar_t *s) {
    const wchar_t *rest = s;
    if (!_wcsnicmp(s, L"Desktop", 7) && (!s[7] || s[7] == L'\\')) rest = s + 7;
    else if (!_wcsnicmp(s, L"Documents", 9) && (!s[9] || s[9] == L'\\')) rest = s + 9;
    else if (((s[0] >= L'A' && s[0] <= L'Z') || (s[0] >= L'a' && s[0] <= L'z')) && s[1] == L':' && s[2] == L'\\') rest = s + 3;
    else return 0;
    if (wcsstr(s, L"..") || wcspbrk(rest, L"/:*?\"<>|") || wcsstr(s, L"\\\\")) return 0;
    return s[wcslen(s) - 1] != L'\\' || rest == s + 3;
}
static int strings(const JsonValue *v, wchar_t **out, size_t *count, int required) {
    const JsonValue *item, *other;
    size_t n = 0;
    if (!v) return !required;
    if (v->kind != JSON_ARRAY) return 0;
    for (item = v->child; item; item = item->next) {
        if (n == PROFILE_MAX_ITEMS || !plain_text(item, 60)) return 0;
        for (other = v->child; other != item; other = other->next)
            if (!_wcsicmp(other->string, item->string)) return 0;
        if (!(out[n] = _wcsdup(item->string))) return 0;
        *count = ++n;
    }
    return n || !required;
}
static int load_one(const JsonValue *v, Profile *p, size_t index, char *error, size_t cap) {
    static const wchar_t *const keys[] = {
        L"id", L"local_id", L"name", L"gender", L"occupation", L"computer_name", L"main_task",
        L"side_tasks", L"interests", L"hobbies", L"randomness_score", L"typo_percent", L"project_path"
    };
    const JsonValue *id = json_get(v, L"id"), *local = json_get(v, L"local_id"), *name = json_get(v, L"name");
    const JsonValue *occupation = json_get(v, L"occupation"), *computer = json_get(v, L"computer_name");
    const JsonValue *side = json_get(v, L"side_tasks"), *score = json_get(v, L"randomness_score");
    const JsonValue *typo = json_get(v, L"typo_percent"), *item;
    int gender = lookup(json_get(v, L"gender"), gender_names, PROFILE_GENDER_COUNT);
    int main_task = lookup(json_get(v, L"main_task"), task_names, PROFILE_TASK_COUNT);
    if (!v || v->kind != JSON_OBJECT || !json_keys(v, keys, sizeof(keys)/sizeof(*keys)))
        return fail_at(error, cap, index, "must be an object with only the documented fields");
    if (!id || id->kind != JSON_NUMBER || !id->number) return fail_at(error, cap, index, "id must be a positive integer");
    p->id = id->number;
    if (!plain_text(local, 32) || !token(local->string, 32))
        return fail_at(error, cap, index, "local_id must be 1-32 letters, digits, '-' or '_'");
    if (!plain_text(name, 80)) return fail_at(error, cap, index, "name must be 1-80 characters on one line");
    if (gender < 0) return fail_at(error, cap, index, "gender must be \"male\" or \"female\"");
    if (!plain_text(occupation, 60)) return fail_at(error, cap, index, "occupation must be 1-60 characters on one line");
    if (!plain_text(computer, 15) || !computer_name(computer->string))
        return fail_at(error, cap, index, "computer_name must be a valid Windows name: 1-15 letters, digits or hyphens");
    if (main_task < 0)
        return fail_at(error, cap, index, "main_task must be programming, browser, excel, notepad, image_viewer, cad or files");
    p->gender = (ProfileGender)gender; p->main_task = (ProfileTask)main_task;
    if (!(p->local_id = _wcsdup(local->string)) || !(p->name = _wcsdup(name->string)) ||
        !(p->occupation = _wcsdup(occupation->string)) || !(p->computer_name = _wcsdup(computer->string)))
        return fail(error, cap, "out of memory");
    if (side) {
        if (side->kind != JSON_ARRAY) return fail_at(error, cap, index, "side_tasks must be a list of task names");
        for (item = side->child; item; item = item->next) {
            int task = lookup(item, task_names, PROFILE_TASK_COUNT);
            size_t i;
            if (task < 0) return fail_at(error, cap, index, "side_tasks contains an unknown task name");
            if (task == main_task) return fail_at(error, cap, index, "side_tasks must not repeat the main_task");
            for (i = 0; i < p->side_task_count; ++i)
                if (p->side_tasks[i] == (ProfileTask)task) return fail_at(error, cap, index, "side_tasks contains a duplicate");
            p->side_tasks[p->side_task_count++] = (ProfileTask)task;
        }
    }
    if (!strings(json_get(v, L"interests"), p->interests, &p->interest_count, 1))
        return fail_at(error, cap, index, "interests must list 1-32 distinct entries of up to 60 characters");
    if (!strings(json_get(v, L"hobbies"), p->hobbies, &p->hobby_count, 0))
        return fail_at(error, cap, index, "hobbies must list up to 32 distinct entries of up to 60 characters");
    if (!score || score->kind != JSON_NUMBER || score->number > 100)
        return fail_at(error, cap, index, "randomness_score must be an integer from 0 to 100");
    if (!typo || typo->kind != JSON_NUMBER || typo->number > 20)
        return fail_at(error, cap, index, "typo_percent must be an integer from 0 to 20");
    p->randomness_score = score->number; p->typo_percent = typo->number;
    { const JsonValue *path = json_get(v, L"project_path");
      if (path) {
          if (!plain_text(path, 200) || !project_path_valid(path->string))
              return fail_at(error, cap, index, "project_path must be Desktop, Documents, a subfolder of either, or an absolute path");
          if (!(p->project_path = _wcsdup(path->string))) return fail(error, cap, "out of memory");
      } }
    return 1;
}
int profile_parse(const wchar_t *text, ProfileSet *set, char *error, size_t cap) {
    static const wchar_t *const keys[] = { L"version", L"profiles" };
    JsonValue *root = NULL;
    const JsonValue *version, *list, *v;
    size_t count = 0, i, j;
    memset(set, 0, sizeof(*set));
    if (!cap) return 0;
    *error = 0;
    if (!json_parse(text, &root, error, cap)) return 0;
    version = json_get(root, L"version"); list = json_get(root, L"profiles");
    if (!json_keys(root, keys, 2) || !version || version->kind != JSON_NUMBER || version->number != 1) {
        json_free(root); return fail(error, cap, "profiles file needs \"version\": 1 and a \"profiles\" list");
    }
    if (!list || list->kind != JSON_ARRAY || !list->child) {
        json_free(root); return fail(error, cap, "\"profiles\" must be a non-empty list");
    }
    for (v = list->child; v; v = v->next) ++count;
    if (count > 256) { json_free(root); return fail(error, cap, "at most 256 profiles are supported"); }
    if (!(set->items = calloc(count, sizeof(*set->items)))) { json_free(root); return fail(error, cap, "out of memory"); }
    for (v = list->child, i = 0; v; v = v->next, ++i) {
        set->count = i+1;
        if (!load_one(v, &set->items[i], i, error, cap)) goto fail;
        for (j = 0; j < i; ++j) {
            if (set->items[j].id == set->items[i].id) { fail_at(error, cap, i, "id is already used by another profile"); goto fail; }
            if (!_wcsicmp(set->items[j].local_id, set->items[i].local_id)) {
                fail_at(error, cap, i, "local_id is already used by another profile"); goto fail;
            }
        }
    }
    json_free(root); return 1;
fail:
    json_free(root); profile_free(set); return 0;
}
int profile_load(const wchar_t *path, ProfileSet *set, char *error, size_t cap) {
    wchar_t *text = NULL;
    int ok;
    memset(set, 0, sizeof(*set));
    if (!read_utf8(path, &text, error, cap)) return 0;
    ok = profile_parse(text, set, error, cap);
    free(text); return ok;
}
void profile_free(ProfileSet *set) {
    size_t i, j;
    if (!set) return;
    for (i = 0; i < set->count; ++i) {
        Profile *p = &set->items[i];
        free(p->local_id); free(p->name); free(p->occupation); free(p->computer_name); free(p->project_path);
        for (j = 0; j < p->interest_count; ++j) free(p->interests[j]);
        for (j = 0; j < p->hobby_count; ++j) free(p->hobbies[j]);
    }
    free(set->items); set->items = NULL; set->count = 0;
}
const Profile *profile_find(const ProfileSet *set, unsigned id) {
    size_t i;
    for (i = 0; set && i < set->count; ++i) if (set->items[i].id == id) return &set->items[i];
    return NULL;
}
const char *profile_task_name(ProfileTask task) {
    static const char *const names[PROFILE_TASK_COUNT] = {
        "programming", "browser", "excel", "notepad", "image_viewer", "cad", "files"
    };
    return (unsigned)task < PROFILE_TASK_COUNT ? names[task] : "unknown";
}
const char *profile_gender_name(ProfileGender gender) {
    return gender == PROFILE_MALE ? "male" : gender == PROFILE_FEMALE ? "female" : "unknown";
}
int profile_project_root(const Profile *p, wchar_t *out, size_t cap) {
    wchar_t base[MAX_PATH];
    const wchar_t *rest;
    int folder;
    if (!p || !p->project_path || cap < MAX_PATH) return 0;
    if (!_wcsnicmp(p->project_path, L"Desktop", 7) && (!p->project_path[7] || p->project_path[7] == L'\\')) {
        folder = CSIDL_DESKTOPDIRECTORY; rest = p->project_path + 7;
    } else if (!_wcsnicmp(p->project_path, L"Documents", 9) && (!p->project_path[9] || p->project_path[9] == L'\\')) {
        folder = CSIDL_PERSONAL; rest = p->project_path + 9;
    } else { wcsncpy(out, p->project_path, cap - 1); out[cap - 1] = 0; return 1; }
    if (FAILED(SHGetFolderPathW(NULL, folder, NULL, SHGFP_TYPE_CURRENT, base))) return 0;
    return swprintf(out, cap, L"%ls%ls", base, rest) > 0;
}
