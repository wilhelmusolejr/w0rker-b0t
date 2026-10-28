#include "reference_config.h"
#include "json_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
static int checks, failures;
#define CHECK(t) do { ++checks; if (!(t)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#t); } } while (0)
static int write_text(const wchar_t *path, const wchar_t *text) {
    int n = WideCharToMultiByte(CP_UTF8,0,text,-1,NULL,0,NULL,NULL), ok;
    char *bytes = malloc((size_t)n);
    FILE *file = _wfopen(path,L"wb");
    if (!bytes || !file) { free(bytes); if (file) fclose(file); return 0; }
    WideCharToMultiByte(CP_UTF8,0,text,-1,bytes,n,NULL,NULL);
    ok = fwrite(bytes,1,(size_t)n-1,file) == (size_t)n-1;
    free(bytes); if (fclose(file)) ok = 0; return ok;
}
static int parse_ok(const wchar_t *source) {
    JsonValue *value = NULL; char error[256]; int ok = json_parse(source,&value,error,sizeof(error));
    json_free(value); return ok;
}
static void json_tests(void) {
    JsonValue *v = NULL;
    char error[256];
    CHECK(parse_ok(L"{\"a\":[true,false,null,0,4294967295,\"x\\n\\t\\u00e9\\uD83D\\uDE00\"]}"));
    CHECK(json_parse(L"{\"text\":\"A\\r\\nB\\\\\\\"\\uD83D\\uDE00\"}",&v,error,sizeof(error)));
    if (v) { const JsonValue *s = json_get(v,L"text"); CHECK(s && !wcscmp(s->string,L"A\r\nB\\\"\xd83d\xde00")); }
    json_free(v);
    CHECK(!parse_ok(L"")); CHECK(!parse_ok(L"{\"x\":1,\"x\":2}"));
    CHECK(!parse_ok(L"[1,]")); CHECK(!parse_ok(L"{\"x\":1,}"));
    CHECK(!parse_ok(L"[01]")); CHECK(!parse_ok(L"[4294967296]"));
    CHECK(!parse_ok(L"[1.5]")); CHECK(!parse_ok(L"[-1]"));
    CHECK(!parse_ok(L"[truefalse]")); CHECK(!parse_ok(L"{} extra"));
    CHECK(!parse_ok(L"\"\\u0000\"")); CHECK(!parse_ok(L"\"\\uD800\""));
    CHECK(!parse_ok(L"\"\\uDC00\"")); CHECK(!parse_ok(L"\"\\x20\""));
    CHECK(!parse_ok(L"\"a\nb\"")); CHECK(!parse_ok(L"\"unterminated"));
    CHECK(!parse_ok(L"[[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]]"));
}
static void schedule_tests(void) {
    CodingSchedule a, b;
    unsigned i;
    coding_schedule_init(&a,300000,600000,120000,180000,42);
    coding_schedule_init(&b,300000,600000,120000,180000,42);
    for (i = 0; i < 2000; ++i) {
        unsigned visit;
        CHECK(a.next_interval_ms >= 300000 && a.next_interval_ms <= 600000);
        CHECK(a.next_interval_ms == b.next_interval_ms);
        CHECK(!coding_schedule_due(&a));
        coding_schedule_add(&a,a.next_interval_ms-1); CHECK(!coding_schedule_due(&a));
        coding_schedule_add(&a,1); CHECK(coding_schedule_due(&a));
        visit = coding_schedule_visit(&a); CHECK(visit >= 120000 && visit <= 180000);
        CHECK(visit == coding_schedule_visit(&b));
        coding_schedule_reset(&a); coding_schedule_reset(&b);
    }
    coding_schedule_add(&a,UINT64_MAX); coding_schedule_add(&a,1);
    CHECK(a.coding_ms == UINT64_MAX && coding_schedule_due(&a));
    coding_schedule_reset(&a); CHECK(a.coding_ms == 0 && !coding_schedule_due(&a));
    coding_schedule_init(&a,300000,300000,120000,120000,0);
    CHECK(a.next_interval_ms == 300000 && coding_schedule_visit(&a) == 120000);
}
static void config_tests(void) {
    wchar_t temp[MAX_PATH], directory[MAX_PATH], config_path[MAX_PATH], output[MAX_PATH], state[MAX_PATH];
    wchar_t json[10000], folder[MAX_PATH], hash_w[65];
    const wchar_t *payload = L"print('hello')\r\nx = 1\n";
    char hash[65], error[512];
    ReferenceConfig c = {0}; size_t i, offset;
    CHECK(reference_sha256(L"abc",hash));
    CHECK(!strcmp(hash,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(reference_sha256(payload,hash));
    for (i = 0; i < 65; ++i) hash_w[i] = (wchar_t)hash[i];
    GetTempPathW(MAX_PATH,temp); swprintf(directory,MAX_PATH,L"%lsta-ref-%lu",temp,GetCurrentProcessId());
    CHECK(CreateDirectoryW(directory,NULL));
    wcscpy(folder,directory); for (i = 0; folder[i]; ++i) if (folder[i] == L'\\') folder[i] = L'/';
    swprintf(config_path,MAX_PATH,L"%ls\\config.json",directory);
    swprintf(output,MAX_PATH,L"%ls\\sample.py",directory);
    swprintf(state,MAX_PATH,L"%ls\\progress.state",directory);
    swprintf(json,10000,L"{\"version\":1,\"destination\":\"%ls\",\"progress_file\":\"progress.state\","
        L"\"countdown_seconds\":10,\"seed\":42,\"line_endings\":\"normalize_lf\","
        L"\"typing\":{\"character_ms\":[140,300],\"newline_ms\":[900,2200],\"pause_every_chars\":[160,320],\"pause_ms\":[6000,14000]},"
        L"\"actions\":{\"pause_ms\":[1400,3000],\"mouse_move_ms\":[1000,1800]},"
        L"\"browser\":{\"enabled\":true,\"coding_interval_seconds\":[300,600],\"visit_seconds\":[120,180],"
        L"\"topics\":[{\"query\":\"Python json docs\",\"url\":\"https://docs.python.org/3/library/json.html\"}]},"
        L"\"files\":[{\"enabled\":true,\"source\":\"reference/sample.py\",\"name\":\"sample.py\",\"sha256\":\"%ls\","
        L"\"content\":\"print('hello')\\r\\nx = 1\\n\"}]}",folder,hash_w);
    CHECK(write_text(config_path,json)); CHECK(reference_config_load(config_path,&c,error,sizeof(error)));
    if (c.file_count) {
        CHECK(c.file_count == 1 && !wcscmp(c.files[0].name,L"sample.py"));
        CHECK(!wcscmp(c.files[0].content,payload)); CHECK(!wcscmp(c.files[0].normalized,L"print('hello')\nx = 1\n"));
        CHECK(c.interval_seconds[0] == 300 && c.visit_seconds[1] == 180 && c.countdown_seconds == 10);
        CHECK(reference_config_preflight(&c,error,sizeof(error)) && !c.files[0].done && !c.files[0].owned);
        CHECK(write_text(output,L"print('hello')\n"));
        CHECK(!reference_config_preflight(&c,error,sizeof(error))); /* Never claim a user's partial file. */
        CHECK(reference_config_claim(&c,0,error,sizeof(error)));
        CHECK(reference_config_preflight(&c,error,sizeof(error)));
        CHECK(c.files[0].owned && !c.files[0].done && c.files[0].offset == 15);
        CHECK(write_text(output,L"print('hello')\r\nx = 1\r\n"));
        CHECK(reference_config_preflight(&c,error,sizeof(error)) && c.files[0].done);
        CHECK(write_text(output,L"print('hello')\nx = 1\n)}"));
        CHECK(!reference_config_preflight(&c,error,sizeof(error)));
        CHECK(strstr(error,"character 21") && strstr(error,"expected U+0000") && strstr(error,"saved U+0029"));
        CHECK(write_text(output,L"print('changed')\n"));
        CHECK(!reference_config_preflight(&c,error,sizeof(error)));
        CHECK(write_text(state,L"TUTORIAL_REFERENCE_PROGRESS 1\nwrong-destination\n"));
        CHECK(!reference_config_preflight(&c,error,sizeof(error)));
    }
    reference_config_free(&c);
    { wchar_t url_path[MAX_PATH], mixed_json[12000], too_long[300];
      const wchar_t *where = wcsstr(json,L"\"topics\"");
      CHECK(where != NULL);
      swprintf(url_path,MAX_PATH,L"%ls\\Apollo Vehicle URLs AU and NZ.txt",directory);
      swprintf(mixed_json,12000,L"%.*ls\"url_list_file\":\"Apollo Vehicle URLs AU and NZ.txt\",%ls",
          (int)(where-json),json,where);
      CHECK(write_text(config_path,mixed_json));
      CHECK(write_text(url_path,L"APOLLO URLS\r\nFleet: https://www.apollocamper.com/campervan-hire-new-zealand\r\n"
          L"https://www.apollocamper.com/campervan-hire-new-zealand\n"
          L"https://www.apollocamper.com/campervan-hire-australia/hitop\n"
          L"https://docs.python.org/3/library/json.html\n"));
      CHECK(reference_config_load(config_path,&c,error,sizeof(error)));
      if (c.topic_count == 3) {
          CHECK(!wcscmp(c.url_list_path,url_path));
          CHECK(c.topics[0].category == REFERENCE_PYTHON && c.topics[0].query != NULL);
          CHECK(c.topics[1].category == REFERENCE_URL && c.topics[1].query == NULL);
          CHECK(!wcscmp(c.topics[1].url,L"https://www.apollocamper.com/campervan-hire-new-zealand"));
          CHECK(c.topics[2].category == REFERENCE_URL && c.topics[2].query == NULL);
      } else CHECK(c.topic_count == 3);
      reference_config_free(&c);
      CHECK(write_text(url_path,L"No URLs in this file\n"));
      CHECK(!reference_config_load(config_path,&c,error,sizeof(error)) && strstr(error,"no HTTP/HTTPS"));
      CHECK(write_text(url_path,L"https://example.com/quote\"in-url\n"));
      CHECK(!reference_config_load(config_path,&c,error,sizeof(error)) && strstr(error,"invalid HTTP/HTTPS"));
      CHECK(write_text(url_path,L"https://example.com/back\\slash\n"));
      CHECK(!reference_config_load(config_path,&c,error,sizeof(error)));
      CHECK(write_text(url_path,L"https://\n"));
      CHECK(!reference_config_load(config_path,&c,error,sizeof(error)));
      wcscpy(too_long,L"https://example.com/");
      wmemset(too_long+wcslen(too_long),L'a',270-wcslen(too_long)); too_long[270] = 0;
      CHECK(write_text(url_path,too_long));
      CHECK(!reference_config_load(config_path,&c,error,sizeof(error)) && strstr(error,"219 characters"));
      CHECK(DeleteFileW(url_path));
      CHECK(!reference_config_load(config_path,&c,error,sizeof(error)));
      CHECK(write_text(config_path,json));
    }
    { wchar_t *ch = wcsstr(json,L"print('hello')"); if (ch) ch[0] = L'P'; }
    CHECK(write_text(config_path,json)); CHECK(!reference_config_load(config_path,&c,error,sizeof(error)));
    CHECK(strstr(error,"SHA-256") != NULL);
    CHECK(reference_prefix(L"a\nb\n",L"a\n",&offset) && offset == 2);
    CHECK(!reference_prefix(L"a\nb\n",L"a\nX",&offset));
    CHECK(!reference_prefix(L"a",L"ab",&offset));
    CHECK(!reference_prefix(L"\xd83d\xde00",L"\xd83d",&offset));
    DeleteFileW(output); DeleteFileW(state); DeleteFileW(config_path); RemoveDirectoryW(directory);
}
static void mixed_choice_tests(void) {
    ReferenceConfig c = {0}; ReferenceTopic topics[27] = {0};
    CodingSchedule a, b;
    size_t previous = SIZE_MAX;
    unsigned counts[REFERENCE_CATEGORY_COUNT] = {0}, seen[27] = {0}, i;
    c.topics = topics; c.topic_count = 27;
    for (i = 0; i < 27; ++i)
        topics[i].category = i < 4 ? REFERENCE_PYTHON : i < 8 ? REFERENCE_JAVASCRIPT : i < 20 ? REFERENCE_URL : REFERENCE_RESEARCH;
    coding_schedule_init(&a,300000,600000,120000,180000,42);
    coding_schedule_init(&b,300000,600000,120000,180000,42);
    for (i = 0; i < 30000; ++i) {
        size_t choice = reference_pick_topic(&c,&a,previous);
        CHECK(choice == reference_pick_topic(&c,&b,previous));
        CHECK(choice < 27 && choice != previous);
        if (choice >= 27) break;
        ++counts[topics[choice].category]; ++seen[choice]; previous = choice;
    }
    /* The four categories with topics split evenly; GitHub has none here. */
    for (i = 0; i < REFERENCE_GITHUB; ++i) CHECK(counts[i] > 6750 && counts[i] < 8250);
    CHECK(!counts[REFERENCE_GITHUB]);
    for (i = 0; i < 27; ++i) CHECK(seen[i] > 0);
    c.topic_count = 1;
    CHECK(reference_pick_topic(&c,&a,0) == 0);
    c.topic_count = 2;
    CHECK(reference_pick_topic(&c,&a,0) == 1);
    CHECK(reference_pick_topic(&c,&a,1) == 0);
    c.topic_count = 0; CHECK(reference_pick_topic(&c,&a,SIZE_MAX) == SIZE_MAX);
    CHECK(reference_pick_topic(NULL,&a,SIZE_MAX) == SIZE_MAX);
    c.topic_count = 1; topics[0].category = REFERENCE_CATEGORY_COUNT;
    CHECK(reference_pick_topic(&c,&a,SIZE_MAX) == SIZE_MAX);
}
static int write_job(const wchar_t *path, const wchar_t *folder, const wchar_t *files, const wchar_t *browser) {
    wchar_t json[6000];
    swprintf(json,6000,L"{\"version\":2,\"destination\":\"%ls\",\"progress_file\":\"progress.state\","
        L"\"countdown_seconds\":10,\"seed\":0,\"files\":%ls,"
        L"\"typing\":{\"character_ms\":[140,300],\"newline_ms\":[900,2200],\"pause_every_chars\":[160,320],\"pause_ms\":[6000,14000]},"
        L"\"actions\":{\"pause_ms\":[1400,3000],\"mouse_move_ms\":[1000,1800]},"
        L"\"browser\":{\"enabled\":true,\"launch\":\"taskbar\",\"coding_interval_seconds\":[300,600],\"visit_seconds\":[120,180]%ls},"
        L"\"auto_restart\":{\"enabled\":true,\"max_restarts\":3,\"wait_seconds\":30}}",folder,files,browser);
    return write_text(path,json);
}
static size_t research_count(const ReferenceConfig *c, const wchar_t *query) {
    size_t i, n = 0;
    for (i = 0; i < c->topic_count; ++i)
        if (c->topics[i].category == REFERENCE_RESEARCH && (!query || !wcscmp(c->topics[i].query,query))) ++n;
    return n;
}
static void job_tests(void) {
    wchar_t temp[MAX_PATH], directory[MAX_PATH], sources[MAX_PATH], path[MAX_PATH], job[MAX_PATH], folder[MAX_PATH];
    static const wchar_t *const research =
        L",\"weights\":{\"research\":3,\"url_list\":0,\"python_docs\":1},"
        L"\"docs_topics\":[{\"query\":\"Python json docs\",\"url\":\"https://docs.python.org/3/library/json.html\"}],"
        L"\"research\":{\"queries_file\":\"queries.txt\",\"results_to_open\":[1,2],\"templates\":{"
        L"\"patterns\":[\"{brand} vs {brand2}\",\"{brand} hire {city}\",\"{city} {city} trip\"],"
        L"\"values\":{\"brand\":[\"Apollo\",\"Britz\"],\"city\":[\"Sydney\"]}}}";
    static const wchar_t *const all_files = L"{\"folder\":\"src\",\"exclude\":[\"skip*\"],\"order\":\"alphabetical\"}";
    char error[512];
    ReferenceConfig c = {0}, d = {0};
    size_t i;
    GetTempPathW(MAX_PATH,temp); swprintf(directory,MAX_PATH,L"%lsta-job-%lu",temp,GetCurrentProcessId());
    CHECK(CreateDirectoryW(directory,NULL));
    wcscpy(folder,directory); for (i = 0; folder[i]; ++i) if (folder[i] == L'\\') folder[i] = L'/';
    swprintf(sources,MAX_PATH,L"%ls\\src",directory); CHECK(CreateDirectoryW(sources,NULL));
    { static const wchar_t *const names[] = {L"b.cjs",L"a.py",L"c.cjs",L"skip-me.cjs",L"notes.txt"};
      for (i = 0; i < 5; ++i) { swprintf(path,MAX_PATH,L"%ls\\%ls",sources,names[i]); CHECK(write_text(path,L"x = 1\r\n")); } }
    swprintf(path,MAX_PATH,L"%ls\\queries.txt",directory);
    CHECK(write_text(path,L"# comment\r\n\r\n  campervan hire Auckland  \r\nAPOLLO VS BRITZ\n"));
    swprintf(job,MAX_PATH,L"%ls\\job.json",directory);

    CHECK(write_job(job,folder,all_files,research));
    CHECK(reference_config_load(job,&c,error,sizeof(error)));
    if (c.file_count == 3) {
        /* Alphabetical folder scan; .txt and excluded files are ignored. */
        CHECK(!wcscmp(c.files[0].name,L"a.py") && !wcscmp(c.files[1].name,L"b.cjs") && !wcscmp(c.files[2].name,L"c.cjs"));
        CHECK(!wcscmp(c.files[0].normalized,L"x = 1\n") && c.files[0].enabled);
    } else CHECK(c.file_count == 3);
    /* 2 file queries + 2 brand pairs (case-insensitive duplicate dropped) + 2 brand-city + 1 repeated-city. */
    CHECK(research_count(&c,NULL) == 6);
    CHECK(research_count(&c,L"campervan hire Auckland") == 1 && research_count(&c,L"Britz vs Apollo") == 1);
    CHECK(research_count(&c,L"Apollo hire Sydney") == 1 && research_count(&c,L"Sydney Sydney trip") == 1);
    CHECK(research_count(&c,L"Apollo vs Apollo") == 0);
    CHECK(c.weights[REFERENCE_RESEARCH] == 3 && c.weights[REFERENCE_URL] == 0 && c.weights[REFERENCE_PYTHON] == 1);
    CHECK(c.results_to_open[0] == 1 && c.results_to_open[1] == 2);
    CHECK(c.browser_launch == REFERENCE_LAUNCH_TASKBAR && c.auto_restart && c.max_restarts == 3 && c.restart_wait_seconds == 30);
    { CodingSchedule s; unsigned counts[REFERENCE_CATEGORY_COUNT] = {0};
      coding_schedule_init(&s,300000,600000,120000,180000,7);
      for (i = 0; i < 40000; ++i) {
          size_t t = reference_pick_topic(&c,&s,SIZE_MAX);
          CHECK(t < c.topic_count); if (t >= c.topic_count) break;
          ++counts[c.topics[t].category];
      }
      /* Research 3 : Python 1; JavaScript has no topics and URL weight is 0. */
      CHECK(counts[REFERENCE_RESEARCH] > 29000 && counts[REFERENCE_RESEARCH] < 31000);
      CHECK(counts[REFERENCE_PYTHON] > 9000 && counts[REFERENCE_PYTHON] < 11000);
      CHECK(!counts[REFERENCE_URL] && !counts[REFERENCE_JAVASCRIPT]);
    }
    reference_config_free(&c);

    /* Shuffle is repeatable for the same seed so resumes keep their order. */
    CHECK(write_job(job,folder,L"{\"folder\":\"src\",\"order\":\"shuffle\",\"shuffle_seed\":99}",research));
    CHECK(reference_config_load(job,&c,error,sizeof(error)) && reference_config_load(job,&d,error,sizeof(error)));
    CHECK(c.file_count == 4 && d.file_count == 4);
    for (i = 0; i < c.file_count && i < d.file_count; ++i) CHECK(!wcscmp(c.files[i].name,d.files[i].name));
    reference_config_free(&c); reference_config_free(&d);

    CHECK(write_job(job,folder,L"{\"folder\":\"src\",\"list\":[\"c.cjs\",\"a.py\"],\"order\":\"listed\"}",research));
    CHECK(reference_config_load(job,&c,error,sizeof(error)) && c.file_count == 2);
    if (c.file_count == 2) CHECK(!wcscmp(c.files[0].name,L"c.cjs") && !wcscmp(c.files[1].name,L"a.py"));
    reference_config_free(&c);

    /* Rejected settings never reach playback. */
    CHECK(write_job(job,folder,L"{\"folder\":\"src\",\"order\":\"listed\"}",research));
    CHECK(!reference_config_load(job,&c,error,sizeof(error)));
    CHECK(write_job(job,folder,L"{\"folder\":\"src\",\"list\":[\"missing.cjs\"],\"order\":\"listed\"}",research));
    CHECK(!reference_config_load(job,&c,error,sizeof(error)));
    CHECK(write_job(job,folder,L"{\"folder\":\"src\",\"include\":[\"*.js\"],\"order\":\"alphabetical\"}",research));
    CHECK(!reference_config_load(job,&c,error,sizeof(error)) && strstr(error,"no .py or .cjs"));
    CHECK(write_job(job,folder,all_files,L",\"research\":{\"templates\":{\"patterns\":[\"{model} hire\"],\"values\":{\"city\":[\"X\"]}}}"));
    CHECK(!reference_config_load(job,&c,error,sizeof(error)) && strstr(error,"{model}"));
    CHECK(write_job(job,folder,all_files,L",\"weights\":{\"research\":0},\"research\":{\"queries_file\":\"queries.txt\"}"));
    CHECK(!reference_config_load(job,&c,error,sizeof(error)));
    CHECK(write_job(job,folder,all_files,L",\"weights\":{\"python_docs\":5},\"research\":{\"queries_file\":\"queries.txt\"}"));
    CHECK(!reference_config_load(job,&c,error,sizeof(error)) && strstr(error,"no browser choices"));
    CHECK(write_job(job,folder,all_files,L",\"research\":{\"queries_file\":\"queries.txt\",\"results_to_open\":[1,4]}"));
    CHECK(!reference_config_load(job,&c,error,sizeof(error)));
    CHECK(write_job(job,folder,all_files,L",\"weights\":{\"videos\":1}"));
    CHECK(!reference_config_load(job,&c,error,sizeof(error)));

    /* GitHub project pages are their own weighted coding category. */
    CHECK(write_job(job,folder,all_files,L",\"weights\":{\"research\":0,\"url_list\":0,\"python_docs\":1,\"js_docs\":0,\"github\":1},"
        L"\"docs_topics\":[{\"query\":\"Python json docs\",\"url\":\"https://docs.python.org/3/library/json.html\"},"
        L"{\"query\":\"streamlit-webrtc GitHub\",\"url\":\"https://github.com/whitphx/streamlit-webrtc\"}]"));
    CHECK(reference_config_load(job,&c,error,sizeof(error)));
    CHECK(c.topic_count == 2 && c.topics[1].category == REFERENCE_GITHUB && c.weights[REFERENCE_GITHUB] == 1);
    { CodingSchedule s; unsigned counts[REFERENCE_CATEGORY_COUNT] = {0};
      coding_schedule_init(&s,300000,600000,120000,180000,11);
      for (i = 0; i < 20000; ++i) {
          size_t t = reference_pick_topic(&c,&s,SIZE_MAX);
          CHECK(t < c.topic_count); if (t >= c.topic_count) break;
          ++counts[c.topics[t].category];
      }
      CHECK(counts[REFERENCE_GITHUB] > 9000 && counts[REFERENCE_GITHUB] < 11000);
    }
    reference_config_free(&c);
    CHECK(write_job(job,folder,all_files,L",\"docs_topics\":[{\"query\":\"x\",\"url\":\"https://gitlab.com/whitphx/streamlit-webrtc\"}]"));
    CHECK(!reference_config_load(job,&c,error,sizeof(error)));

    { static const wchar_t *const names[] = {L"b.cjs",L"a.py",L"c.cjs",L"skip-me.cjs",L"notes.txt"};
      for (i = 0; i < 5; ++i) { swprintf(path,MAX_PATH,L"%ls\\%ls",sources,names[i]); DeleteFileW(path); } }
    swprintf(path,MAX_PATH,L"%ls\\queries.txt",directory); DeleteFileW(path);
    DeleteFileW(job); RemoveDirectoryW(sources); RemoveDirectoryW(directory);
}
int main(void) {
    json_tests(); schedule_tests(); config_tests(); mixed_choice_tests(); job_tests();
    printf("%d checks, %d failures (no desktop input generated)\n",checks,failures);
    return failures ? 1 : 0;
}
