/* Real delayed file replacement plus virtual-time timeout/cancellation checks. No desktop input. */
#include "saved_text_wait.h"
#include "reference_config.h"
#include "script_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static int checks, failures;
#define CHECK(t) do { ++checks; if (!(t)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#t); } } while (0)
static int write_text(const wchar_t *path, const wchar_t *text) {
    char *bytes;
    int size = WideCharToMultiByte(CP_UTF8,0,text,-1,NULL,0,NULL,NULL), ok;
    FILE *file;
    if (!size || !(bytes = malloc((size_t)size))) return 0;
    WideCharToMultiByte(CP_UTF8,0,text,-1,bytes,size,NULL,NULL);
    file = _wfopen(path,L"wb");
    if (!file) { free(bytes); return 0; }
    ok = fwrite(bytes,1,(size_t)size-1,file) == (size_t)size-1;
    free(bytes); if (fclose(file)) ok = 0; return ok;
}
typedef struct {
    const wchar_t *path, *publish;
    uint64_t tick, publish_at;
    unsigned waits, cancel_at;
    int virtual_time;
} Context;
static uint64_t now(void *pointer) {
    Context *c = pointer; return c->virtual_time ? c->tick : GetTickCount64();
}
static int poll(void *pointer) { (void)pointer; return 1; }
static int wait_time(void *pointer, unsigned ms) {
    Context *c = pointer;
    if (++c->waits == c->cancel_at) return 0;
    if (c->virtual_time) c->tick += ms; else Sleep(ms);
    return 1;
}
static int read_snapshot(void *pointer, wchar_t **snapshot) {
    Context *c = pointer;
    char error[256]; wchar_t *text = NULL;
    *snapshot = NULL;
    /* A busy editor can still have an older valid prefix on disk. */
    if (c->publish) {
        if (now(c) < c->publish_at) return 0;
        if (!write_text(c->path,c->publish)) return 0;
        c->publish = NULL;
    }
    if (!read_utf8(c->path,&text,error,sizeof(error))) return 0;
    *snapshot = reference_normalize(text); free(text);
    return *snapshot != NULL;
}
typedef struct { const wchar_t *path; int ok; } DelayedSave;
static DWORD WINAPI delayed_save(void *pointer) {
    DelayedSave *save = pointer;
    wchar_t next[MAX_PATH];
    Sleep(650);
    swprintf(next,MAX_PATH,L"%ls.next",save->path);
    save->ok = write_text(next,L"abcdef") && MoveFileExW(next,save->path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if (!save->ok) DeleteFileW(next);
    return 0;
}
int main(void) {
    wchar_t temp[MAX_PATH], path[MAX_PATH];
    Context c = {0}; SavedTextResult result;
    SavedTextIO io = {&c,read_snapshot,poll,wait_time,now};
    size_t required = 6;
    HANDLE worker;
    DelayedSave save;
    uint64_t started;
    CHECK(GetTempPathW(MAX_PATH,temp) != 0);
    CHECK(GetTempFileNameW(temp,L"tcs",0,path) != 0);
    c.path = path;
    CHECK(write_text(path,L"abc"));
    save.path = path; save.ok = 0;
    worker = CreateThread(NULL,0,delayed_save,&save,0,NULL); CHECK(worker != NULL);
    started = GetTickCount64();
    CHECK(saved_text_wait(L"abcdef",&required,&io,3000,&result) == SAVED_TEXT_READY);
    CHECK(result.length == 6 && GetTickCount64()-started >= 600 && c.waits >= 6);
    if (worker) { CHECK(WaitForSingleObject(worker,3000) == WAIT_OBJECT_0); CloseHandle(worker); CHECK(save.ok); }

    memset(&c,0,sizeof(c)); c.path = path; c.virtual_time = 1;
    CHECK(write_text(path,L"abc"));
    CHECK(saved_text_wait(L"abcdef",&required,&io,500,&result) == SAVED_TEXT_TIMEOUT);
    CHECK(result.length == 3 && c.tick == 500 && c.waits == 5);

    /* Unknown resume offsets also wait for a dirty editor's newer save. */
    memset(&c,0,sizeof(c)); c.path = path; c.virtual_time = 1; c.publish = L"abcdef"; c.publish_at = 500;
    CHECK(saved_text_wait(L"abcdef",NULL,&io,1000,&result) == SAVED_TEXT_READY);
    CHECK(result.length == 6 && c.tick == 500 && c.waits == 5);

    memset(&c,0,sizeof(c)); c.path = path; c.virtual_time = 1;
    CHECK(write_text(path,L"abX"));
    CHECK(saved_text_wait(L"abcdef",&required,&io,30000,&result) == SAVED_TEXT_DIFFERENT);
    CHECK(result.mismatch == 2 && result.expected_character == L'c' && result.saved_character == L'X' && c.waits == 0);

    CHECK(write_text(path,L"abcdef")); required = 3;
    CHECK(saved_text_wait(L"abcdef",&required,&io,30000,&result) == SAVED_TEXT_DIFFERENT);
    CHECK(result.length == 6 && c.waits == 0);

    memset(&c,0,sizeof(c)); c.path = path; c.virtual_time = 1; c.cancel_at = 1; required = 6;
    CHECK(write_text(path,L"abc"));
    CHECK(saved_text_wait(L"abcdef",&required,&io,30000,&result) == SAVED_TEXT_CANCELLED);
    CHECK(result.length == 3 && c.waits == 1);

    memset(&c,0,sizeof(c)); c.path = path; c.virtual_time = 1;
    CHECK(saved_text_wait(L"abcdef",NULL,&io,30000,&result) == SAVED_TEXT_READY && result.length == 3);
    CHECK(DeleteFileW(path)); c.publish = L"abcdef"; c.publish_at = 300;
    CHECK(saved_text_wait(L"abcdef",&required,&io,1000,&result) == SAVED_TEXT_READY && c.tick == 300);
    required = 7;
    CHECK(saved_text_wait(L"abcdef",&required,&io,1000,&result) == SAVED_TEXT_INVALID);
    CHECK(saved_text_wait(L"abcdef",NULL,NULL,1000,&result) == SAVED_TEXT_INVALID);
    CHECK(saved_text_wait(L"abcdef",NULL,&io,1000,NULL) == SAVED_TEXT_INVALID);
    DeleteFileW(path);
    printf("Saved checkpoint waiting: %d checks, %d failures; no desktop input\n",checks,failures);
    return failures ? 1 : 0;
}
