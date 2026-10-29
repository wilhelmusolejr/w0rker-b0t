/* Telegram monitor for tutorial playback. Runs beside tutorial.exe, never inside it, so a crash
   or stall in playback is still reported. Reads the playback log, takes screenshots on request,
   and forwards /pause /resume /stop through named events. It never sends desktop input. */
#define COBJMACROS
#include <windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "json_config.h"
#include "script_engine.h"
#include "window_manager.h"
#include "playback_controls.h"
#include "loggy3.h"

typedef struct {
    wchar_t token[128], root[MAX_PATH], logs[MAX_PATH], launcher[MAX_PATH];
    char chat[32];
    unsigned heartbeat_minutes, stall_minutes;
} MonitorConfig;

#define TAIL_LINES 25
typedef struct {
    wchar_t path[MAX_PATH];
    long long offset;
    char partial[1024];
    char time[16], current_file[200], checkpoint[64], last_visit[400], next_visit[64];
    char task[200], target[32]; /* from a work session's SESSION TASK / SESSION TARGET lines */
    char tail[TAIL_LINES][400];
    char reason[800];
    unsigned tail_count, tail_next;
    int paused, finished;
    ULONGLONG last_change;
} LogState;

static MonitorConfig cfg;
static LogState state;
static CRITICAL_SECTION state_lock, send_lock;
static HINTERNET http;
static FILE *own_log;

static void note(const char *format, ...) {
    SYSTEMTIME now; va_list args;
    GetLocalTime(&now);
    printf("[%02u:%02u:%02u] ", now.wHour, now.wMinute, now.wSecond);
    va_start(args, format); vprintf(format, args); va_end(args); putchar('\n'); fflush(stdout);
    if (own_log) {
        fprintf(own_log, "[%04u-%02u-%02u %02u:%02u:%02u] ", now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond);
        va_start(args, format); vfprintf(own_log, format, args); va_end(args);
        fputc('\n', own_log); fflush(own_log);
    }
}

/* ---------- configuration ---------- */
static int load_config(const wchar_t *path) {
    static const wchar_t *const keys[] = {L"telegram_bot_token", L"telegram_chat_id", L"heartbeat_minutes",
        L"stall_minutes", L"log_folder", L"launcher"};
    wchar_t *text = NULL, full[MAX_PATH], *slash;
    char error[256];
    JsonValue *root = NULL;
    const JsonValue *v;
    size_t i;
    int ok = 0;
    if (!GetFullPathNameW(path, MAX_PATH, full, NULL) || !(slash = wcsrchr(full, L'\\'))) return 0;
    if (!read_utf8(full, &text, error, sizeof(error))) { fprintf(stderr, "%s\n", error); return 0; }
    if (!json_parse(text, &root, error, sizeof(error))) { fprintf(stderr, "%s\n", error); free(text); return 0; }
    free(text);
    *slash = 0; wcscpy(cfg.root, full);
    if (!json_keys(root, keys, 6)) { fputs("notify.json has an unknown setting\n", stderr); goto done; }
    v = json_get(root, L"telegram_bot_token");
    if (!v || v->kind != JSON_STRING || wcslen(v->string) < 20 || wcslen(v->string) > 120 || !wcschr(v->string, L':')) {
        fputs("telegram_bot_token is missing; create a bot with @BotFather and paste its token\n", stderr); goto done;
    }
    for (i = 0; v->string[i]; ++i) {
        wchar_t ch = v->string[i];
        if (!((ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
              ch == L':' || ch == L'_' || ch == L'-')) { fputs("telegram_bot_token has invalid characters\n", stderr); goto done; }
    }
    wcscpy(cfg.token, v->string);
    v = json_get(root, L"telegram_chat_id");
    if (!v || v->kind != JSON_STRING || wcslen(v->string) > 20) { fputs("telegram_chat_id must be text, e.g. \"123456789\"\n", stderr); goto done; }
    for (i = 0; v->string[i]; ++i) {
        if (!((v->string[i] >= L'0' && v->string[i] <= L'9') || (i == 0 && v->string[i] == L'-'))) {
            fputs("telegram_chat_id must contain only digits (leave it \"\" to discover it)\n", stderr); goto done;
        }
        cfg.chat[i] = (char)v->string[i];
    }
    cfg.chat[i] = 0;
    v = json_get(root, L"heartbeat_minutes");
    if (!v || v->kind != JSON_NUMBER || v->number > 1440) { fputs("heartbeat_minutes must be 0-1440 (0 = only on request)\n", stderr); goto done; }
    cfg.heartbeat_minutes = v->number;
    v = json_get(root, L"stall_minutes");
    if (!v || v->kind != JSON_NUMBER || v->number < 2 || v->number > 1440) { fputs("stall_minutes must be 2-1440\n", stderr); goto done; }
    cfg.stall_minutes = v->number;
    v = json_get(root, L"log_folder");
    if (!v || v->kind != JSON_STRING || swprintf(full, MAX_PATH, L"%ls\\%ls", cfg.root, v->string) < 0 ||
        !GetFullPathNameW(full, MAX_PATH, cfg.logs, NULL)) { fputs("log_folder is invalid\n", stderr); goto done; }
    v = json_get(root, L"launcher");
    if (!v || v->kind != JSON_STRING || swprintf(full, MAX_PATH, L"%ls\\%ls", cfg.root, v->string) < 0 ||
        !GetFullPathNameW(full, MAX_PATH, cfg.launcher, NULL) ||
        GetFileAttributesW(cfg.launcher) == INVALID_FILE_ATTRIBUTES) { fputs("launcher file was not found\n", stderr); goto done; }
    ok = 1;
done:
    json_free(root);
    return ok;
}

/* ---------- HTTPS to api.telegram.org ---------- */
/* One request: 1 on HTTP 200, 0 on another HTTP status, -1 when the network request itself failed. */
static int telegram_once(const wchar_t *method, const char *content_type, const void *body, DWORD length,
                         char **response, DWORD receive_timeout_ms) {
    HINTERNET connection = NULL, request = NULL;
    wchar_t path[256], header[160];
    DWORD status = 0, size = sizeof(status), used = 0, capacity = 0;
    char *buffer = NULL;
    int ok = 0;
    if (response) *response = NULL;
    swprintf(path, 256, L"/bot%ls/%ls", cfg.token, method);
    if (!(connection = WinHttpConnect(http, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0)) ||
        !(request = WinHttpOpenRequest(connection, body ? L"POST" : L"GET", path, NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE))) goto done;
    WinHttpSetTimeouts(request, 15000, 15000, 60000, receive_timeout_ms);
    if (content_type) swprintf(header, 160, L"Content-Type: %hs\r\n", content_type);
    if (!WinHttpSendRequest(request, content_type ? header : WINHTTP_NO_ADDITIONAL_HEADERS, content_type ? (DWORD)-1L : 0,
            (LPVOID)body, length, length, 0) || !WinHttpReceiveResponse(request, NULL) ||
        !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &size, NULL)) goto done;
    for (;;) {
        DWORD available = 0, read = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) goto done;
        if (!available) break;
        if (used + available + 1 > capacity) {
            char *grown;
            capacity = (used + available + 1) * 2;
            if (capacity > 8 * 1024 * 1024 || !(grown = realloc(buffer, capacity))) goto done;
            buffer = grown;
        }
        if (!WinHttpReadData(request, buffer + used, available, &read)) goto done;
        used += read;
    }
    if (!buffer && !(buffer = malloc(1))) goto done;
    buffer[used] = 0;
    ok = status == 200;
    if (!ok) note("Telegram %ls returned HTTP %lu: %.200s", method, status, buffer);
done:
    if (!ok && status == 0) note("Telegram %ls request failed (error %lu)", method, GetLastError());
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (ok && response) *response = buffer; else free(buffer);
    return !ok && status == 0 ? -1 : ok;
}
/* Short network drops (for example while Remote Desktop reconnects) end in a timeout; try once more. */
static int telegram(const wchar_t *method, const char *content_type, const void *body, DWORD length,
                    char **response, DWORD receive_timeout_ms) {
    int result = telegram_once(method, content_type, body, length, response, receive_timeout_ms);
    if (result >= 0) return result;
    Sleep(3000);
    result = telegram_once(method, content_type, body, length, response, receive_timeout_ms);
    if (result > 0) note("Telegram %ls succeeded on the second try", method);
    return result > 0;
}
static void url_encode(const char *text, char *out, size_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    for (; *text && used + 4 < capacity; ++text) {
        unsigned char ch = (unsigned char)*text;
        if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') out[used++] = (char)ch;
        else { out[used++] = '%'; out[used++] = hex[ch >> 4]; out[used++] = hex[ch & 15]; }
    }
    out[used] = 0;
}
/* Cut UTF-8 text to a byte limit without splitting a character. */
static void clip_utf8(char *text, size_t limit) {
    size_t length = strlen(text);
    if (length <= limit) return;
    while (limit && ((unsigned char)text[limit] & 0xC0) == 0x80) --limit;
    text[limit] = 0;
}
static int send_text_to(const char *chat, const char *text) {
    static char body[16000];
    char clipped[3600], encoded[12000];
    int ok;
    snprintf(clipped, sizeof(clipped), "%s", text); clip_utf8(clipped, 3500);
    EnterCriticalSection(&send_lock);
    url_encode(clipped, encoded, sizeof(encoded));
    snprintf(body, sizeof(body), "chat_id=%s&disable_web_page_preview=true&text=%s", chat, encoded);
    ok = telegram(L"sendMessage", "application/x-www-form-urlencoded", body, (DWORD)strlen(body), NULL, 30000);
    LeaveCriticalSection(&send_lock);
    return ok;
}
static int send_text(const char *text) { return *cfg.chat && send_text_to(cfg.chat, text); }

/* ---------- screenshot (GDI capture, WIC JPEG encode) ---------- */
static int capture_jpeg(unsigned char **jpeg, DWORD *jpeg_size) {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC screen = NULL, memory = NULL;
    HBITMAP bitmap = NULL;
    BITMAPINFO info = {0};
    void *bits = NULL;
    IWICImagingFactory *factory = NULL;
    IWICBitmap *source = NULL;
    IWICFormatConverter *converter = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *options = NULL;
    IStream *stream = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    HRESULT initialized;
    int ok = 0;
    *jpeg = NULL; *jpeg_size = 0;
    if (width <= 0 || height <= 0) return 0;
    initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 0;
    info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    if (!(screen = GetDC(NULL)) || !(memory = CreateCompatibleDC(screen)) ||
        !(bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, NULL, 0))) goto done;
    SelectObject(memory, bitmap);
    if (!BitBlt(memory, 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT)) goto done;
    { CURSORINFO cursor = {sizeof(cursor)};
      ICONINFO icon;
      /* Draw the pointer so the mouse position is visible in the picture. */
      if (GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) && GetIconInfo(cursor.hCursor, &icon)) {
          DrawIconEx(memory, cursor.ptScreenPos.x - x - (int)icon.xHotspot, cursor.ptScreenPos.y - y - (int)icon.yHotspot,
              cursor.hCursor, 0, 0, 0, NULL, DI_NORMAL);
          if (icon.hbmMask) DeleteObject(icon.hbmMask);
          if (icon.hbmColor) DeleteObject(icon.hbmColor);
      } }
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory)) ||
        FAILED(IWICImagingFactory_CreateBitmapFromMemory(factory, width, height, &GUID_WICPixelFormat32bppBGR,
            (UINT)width * 4, (UINT)width * 4 * (UINT)height, bits, &source)) ||
        FAILED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) ||
        FAILED(IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)source, &GUID_WICPixelFormat24bppBGR,
            WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)) ||
        FAILED(CreateStreamOnHGlobal(NULL, TRUE, &stream)) ||
        FAILED(IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatJpeg, NULL, &encoder)) ||
        FAILED(IWICBitmapEncoder_Initialize(encoder, stream, WICBitmapEncoderNoCache)) ||
        FAILED(IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &options))) goto done;
    { PROPBAG2 option = {0}; VARIANT quality;
      option.pstrName = L"ImageQuality"; VariantInit(&quality); quality.vt = VT_R4; quality.fltVal = 0.75f;
      IPropertyBag2_Write(options, 1, &option, &quality); }
    if (FAILED(IWICBitmapFrameEncode_Initialize(frame, options)) ||
        FAILED(IWICBitmapFrameEncode_SetSize(frame, (UINT)width, (UINT)height)) ||
        FAILED(IWICBitmapFrameEncode_SetPixelFormat(frame, &format)) ||
        FAILED(IWICBitmapFrameEncode_WriteSource(frame, (IWICBitmapSource *)converter, NULL)) ||
        FAILED(IWICBitmapFrameEncode_Commit(frame)) || FAILED(IWICBitmapEncoder_Commit(encoder))) goto done;
    { STATSTG stat; HGLOBAL global; void *data;
      if (FAILED(IStream_Stat(stream, &stat, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(stream, &global)) ||
          stat.cbSize.QuadPart > 9 * 1024 * 1024 || !(data = GlobalLock(global))) goto done;
      *jpeg_size = stat.cbSize.LowPart;
      if ((*jpeg = malloc(*jpeg_size))) { memcpy(*jpeg, data, *jpeg_size); ok = 1; }
      GlobalUnlock(global); }
done:
    if (options) IPropertyBag2_Release(options);
    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (encoder) IWICBitmapEncoder_Release(encoder);
    if (stream) IStream_Release(stream);
    if (converter) IWICFormatConverter_Release(converter);
    if (source) IWICBitmap_Release(source);
    if (factory) IWICImagingFactory_Release(factory);
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(NULL, screen);
    CoUninitialize();
    return ok;
}
/* Sends a picture of the desktop. With save_path, the same JPEG is also kept on disk. */
static int send_photo_saved(const char *caption, const wchar_t *save_path) {
    static const char boundary[] = "TutorialMonitorBoundary7d3a91";
    unsigned char *jpeg = NULL;
    DWORD jpeg_size = 0;
    char head[2400], clipped[1000], tail[64], content_type[96];
    char *body;
    size_t head_length, tail_length;
    int ok;
    if (!*cfg.chat && !save_path) return 0;
    if (!window_desktop_ready()) {
        char text[1200];
        snprintf(text, sizeof(text), "Screenshot unavailable: the desktop is locked or disconnected.\n\n%s", caption);
        return send_text(text);
    }
    if (!capture_jpeg(&jpeg, &jpeg_size)) {
        char text[1200];
        snprintf(text, sizeof(text), "Screenshot capture failed.\n\n%s", caption);
        return send_text(text);
    }
    if (save_path) {
        FILE *out = _wfopen(save_path, L"wb");
        if (out && fwrite(jpeg, 1, jpeg_size, out) == jpeg_size && !fclose(out)) note("saved screenshot %ls", save_path);
        else { if (out) fclose(out); note("could not save screenshot %ls", save_path); }
    }
    if (!*cfg.chat) { free(jpeg); return 1; }
    snprintf(clipped, sizeof(clipped), "%s", caption); clip_utf8(clipped, 950);
    head_length = (size_t)snprintf(head, sizeof(head),
        "--%s\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n%s\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n%s\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"screen.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n", boundary, cfg.chat, boundary, clipped, boundary);
    tail_length = (size_t)snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", boundary);
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    if (!(body = malloc(head_length + jpeg_size + tail_length))) { free(jpeg); return 0; }
    memcpy(body, head, head_length); memcpy(body + head_length, jpeg, jpeg_size);
    memcpy(body + head_length + jpeg_size, tail, tail_length);
    EnterCriticalSection(&send_lock);
    ok = telegram(L"sendPhoto", content_type, body, (DWORD)(head_length + jpeg_size + tail_length), NULL, 60000);
    LeaveCriticalSection(&send_lock);
    free(body); free(jpeg);
    return ok;
}
static int send_photo(const char *caption) { return send_photo_saved(caption, NULL); }

/* ---------- playback state ---------- */
static int tutorial_running(void) {
    PROCESSENTRY32W entry = {sizeof(entry)};
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    int found = 0;
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(snapshot, &entry))
        do found |= !_wcsicmp(entry.szExeFile, L"tutorial.exe"); while (!found && Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return found;
}
static int newest_log(wchar_t path[MAX_PATH]) {
    WIN32_FIND_DATAW found;
    wchar_t pattern[MAX_PATH];
    FILETIME best = {0, 0};
    HANDLE search;
    int any = 0;
    swprintf(pattern, MAX_PATH, L"%ls\\playback-*.log", cfg.logs);
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        if (CompareFileTime(&found.ftLastWriteTime, &best) > 0) {
            best = found.ftLastWriteTime; any = 1;
            swprintf(path, MAX_PATH, L"%ls\\%ls", cfg.logs, found.cFileName);
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return any;
}
static void copy_after(char *out, size_t capacity, const char *message, const char *prefix) {
    snprintf(out, capacity, "%s", message + strlen(prefix));
}
/* Updates the summary from one log line; returns an event message to send, if any. */
static int read_line(char *line, char *event, size_t capacity) {
    char *message = strstr(line, ": "), *bracket;
    *event = 0;
    if (line[0] != '[' || !(bracket = strchr(line, ']')) || !message) return 0;
    snprintf(state.time, sizeof(state.time), "%.*s", (int)(bracket - line - 1), line + 1);
    message += 2;
    if (!strstr(message, "presentation pause") && !strstr(message, "moving to control") &&
        strncmp(message, "browser scroll ", 15) && strncmp(message, "starting in", 11)) {
        snprintf(state.tail[state.tail_next], sizeof(state.tail[0]), "%s %s", state.time, message);
        state.tail_next = (state.tail_next + 1) % TAIL_LINES;
        if (state.tail_count < TAIL_LINES) ++state.tail_count;
    }
    if (!strncmp(message, "SESSION TASK: ", 14)) {
        copy_after(state.task, sizeof(state.task), message, "SESSION TASK: ");
        state.current_file[0] = 0;
    } else if (!strncmp(message, "SESSION TARGET: ", 16)) {
        char *end;
        copy_after(state.target, sizeof(state.target), message, "SESSION TARGET: ");
        if ((end = strstr(state.target, " today"))) *end = 0;
    } else if (!strncmp(message, "SESSION ALERT: ", 15)) {
        snprintf(event, capacity, "ALERT: %s", message + 15);
    } else if (!strncmp(message, "SESSION SUMMARY: ", 17)) {
        state.finished = 1;
        snprintf(event, capacity, "%s", message + 17);
    } else if ((!strncmp(message, "typing ", 7) || !strncmp(message, "resuming ", 9)) && strstr(message, " characters)")) {
        /* "typing src/App.jsx (1234 characters)" */
        char *open = strrchr(message, '(');
        const char *name = message + (message[0] == 't' ? 7 : 9);
        snprintf(state.current_file, sizeof(state.current_file), "%.*s", open ? (int)(open - name - 1) : 199, name);
    } else if (!strncmp(message, "side app break: ", 16)) {
        copy_after(state.current_file, sizeof(state.current_file), message, "side app break: ");
    } else if (!strncmp(message, "reference ", 10) && strchr(message, '/') && strstr(message, ": "))
        copy_after(state.current_file, sizeof(state.current_file), message, "reference ");
    else if (!strncmp(message, "verified reference checkpoint: ", 31))
        copy_after(state.checkpoint, sizeof(state.checkpoint), message, "verified reference checkpoint: ");
    else if (!strncmp(message, "research query: ", 16))
        snprintf(state.last_visit, sizeof(state.last_visit), "%s research: %s", state.time, message + 16);
    else if (!strncmp(message, "browser category: ", 18))
        snprintf(state.last_visit, sizeof(state.last_visit), "%s %s", state.time, message + 18);
    else if (!strncmp(message, "opening search result ", 22)) {
        size_t used = strlen(state.last_visit);
        snprintf(state.last_visit + used, sizeof(state.last_visit) - used, " -> %.120s", message + 22);
    } else if (!strncmp(message, "next browser visit in ", 22))
        copy_after(state.next_visit, sizeof(state.next_visit), message, "next browser visit in ");
    else if (strstr(message, "paused; F8 resumes") || !strncmp(message, "paused remotely", 15)) state.paused = 1;
    else if (!strncmp(message, "resumed", 7)) state.paused = 0;
    else if (strstr(message, "F8 pause/resume | F9 stop")) {
        state.paused = state.finished = 0;
        snprintf(event, capacity, "Playback started (countdown running).");
    } else if (!strncmp(message, "completed ", 10) && strstr(message, ": original filename")) {
        char *colon = strstr(message, ": original filename");
        snprintf(event, capacity, "Finished %.*s", (int)(colon - message - 10), message + 10);
    } else if (strstr(message, "all enabled reference files completed")) {
        state.finished = 1; snprintf(event, capacity, "All reference files are complete and verified.");
    } else if (!strncmp(message, "unexpected stop; automatic restart", 34)) {
        snprintf(event, capacity, "Unexpected stop - %s. Will resume after the wait.", message + 17);
    } else if (!strcmp(message, "stopped before completion")) {
        /* The few lines logged just before the stop say why; keep them for the alert. */
        unsigned i, count = state.tail_count < 4 ? state.tail_count : 4;
        size_t used = 0;
        state.reason[0] = 0;
        for (i = count; i > 1 && used < sizeof(state.reason); --i)
            used += (size_t)snprintf(state.reason + used, sizeof(state.reason) - used, "%s%s", used ? "\n" : "",
                state.tail[(state.tail_next + TAIL_LINES - i) % TAIL_LINES]);
        state.paused = 0; return 2;
    } else if (!strncmp(message, "restart check failed: ", 22) || !strncmp(message, "startup check failed: ", 22) ||
               strstr(message, "preflight failed: ")) {
        snprintf(state.reason, sizeof(state.reason), "%s %s", state.time, message);
        return 3;
    } else if (!strcmp(message, "auto-restart cancelled")) {
        snprintf(event, capacity, "Auto-restart cancelled; playback has ended.");
    }
    return *event != 0;
}
/* Reads new log text; returns 3 when a start or restart check failed, 2 when playback reported
   a stop, 1 for another event. */
static int poll_log(int quiet, char events[][200], unsigned *event_count) {
    wchar_t path[MAX_PATH];
    HANDLE file;
    LARGE_INTEGER size;
    char buffer[8192];
    int result = 0;
    *event_count = 0;
    if (!newest_log(path)) return 0;
    if (wcscmp(path, state.path)) {
        memset(&state, 0, sizeof(state));
        wcscpy(state.path, path); state.last_change = GetTickCount64();
    }
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (GetFileSizeEx(file, &size) && size.QuadPart > state.offset) {
        LARGE_INTEGER position; position.QuadPart = state.offset;
        SetFilePointerEx(file, position, NULL, FILE_BEGIN);
        state.last_change = GetTickCount64();
        for (;;) {
            DWORD read = 0, i;
            if (!ReadFile(file, buffer, sizeof(buffer), &read, NULL) || !read) break;
            state.offset += read;
            for (i = 0; i < read; ++i) {
                size_t used = strlen(state.partial);
                if (buffer[i] == '\r') continue;
                if (buffer[i] != '\n') {
                    if (used + 1 < sizeof(state.partial)) { state.partial[used] = buffer[i]; state.partial[used+1] = 0; }
                    continue;
                }
                { char event[200]; int kind = read_line(state.partial, event, sizeof(event));
                  state.partial[0] = 0;
                  if (quiet || !kind) continue;
                  if (kind >= 2) { if (kind > result) result = kind; }
                  else if (*event_count < 8) snprintf(events[(*event_count)++], 200, "%s", event);
                  if (!result) result = 1; }
            }
        }
    }
    CloseHandle(file);
    return result;
}
/* Loggy3 and today's recorded hours, read from the app itself (accessibility, no input). */
static size_t loggy_text(char *out, size_t capacity) {
    HWND w = loggy_window(NULL);
    LoggyToday today;
    char timer[32] = "", hours[48] = "not on screen";
    long seconds;
    if (!w) return (size_t)snprintf(out, capacity, "Loggy3: not open\n");
    if (!loggy_today(w, NULL, &today)) return (size_t)snprintf(out, capacity, "Loggy3: could not be read\n");
    if (today.timer_seconds >= 0)
        snprintf(timer, sizeof(timer), " (timer %ld:%02ld:%02ld)", today.timer_seconds / 3600, today.timer_seconds / 60 % 60, today.timer_seconds % 60);
    if ((seconds = loggy_today_seconds(&today)) >= 0) snprintf(hours, sizeof(hours), "%ldh %02ldm recorded", seconds / 3600, seconds / 60 % 60);
    return (size_t)snprintf(out, capacity, "Loggy3: %s%s\nToday (UTC day): %s%s%s\n",
        today.state == LOGGY_RECORDING ? "RECORDING" : today.state == LOGGY_STOPPED ? "not recording" :
        today.state == LOGGY_BUSY ? "busy (switching)" : "unknown", timer, hours,
        *state.target ? " of " : "", *state.target ? state.target : "");
}
static void status_text(char *out, size_t capacity, int with_tail) {
    int running = tutorial_running(), desktop = window_desktop_ready();
    ULONGLONG idle;
    size_t used;
    unsigned i;
    char loggy[300];
    loggy_text(loggy, sizeof(loggy));
    EnterCriticalSection(&state_lock);
    idle = state.last_change ? (GetTickCount64() - state.last_change) / 60000 : 0;
    /* "Now:" only during a work session; other playback keeps its File line below. */
    if (*state.task) used = (size_t)snprintf(out, capacity, "%sNow: %s%s%s\n", loggy, state.task,
        *state.current_file ? " | " : "", state.current_file);
    else used = (size_t)snprintf(out, capacity, "%s", loggy);
    if (used < capacity) used += (size_t)snprintf(out + used, capacity - used,
        "Playback: %s\nDesktop: %s\nFile: %s\nProgress: %s characters\nNext browser visit: %s\nLast visit: %s\n"
        "Last log line: %s (%llu min ago)",
        !running ? (state.finished ? "finished" : "NOT RUNNING") : state.paused ? "PAUSED" : "running",
        desktop ? "unlocked" : "LOCKED or disconnected",
        *state.current_file ? state.current_file : "-", *state.checkpoint ? state.checkpoint : "-",
        *state.next_visit ? state.next_visit : "-", *state.last_visit ? state.last_visit : "-",
        *state.time ? state.time : "-", (unsigned long long)idle);
    if (with_tail && used < capacity) {
        used += (size_t)snprintf(out + used, capacity - used, "\n\nRecent log:");
        for (i = 0; i < state.tail_count && used < capacity; ++i) {
            unsigned index = (state.tail_next + TAIL_LINES - state.tail_count + i) % TAIL_LINES;
            used += (size_t)snprintf(out + used, capacity - used, "\n%s", state.tail[index]);
        }
    }
    LeaveCriticalSection(&state_lock);
}

/* ---------- commands ---------- */
static int signal_playback(const wchar_t *name) {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    int ok = event && SetEvent(event);
    if (event) CloseHandle(event);
    return ok;
}
static void run_launcher(char *reply, size_t capacity) {
    wchar_t command[MAX_PATH * 2 + 32];
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    if (tutorial_running()) { snprintf(reply, capacity, "Playback is already running."); return; }
    if (!window_desktop_ready()) { snprintf(reply, capacity, "Not started: the desktop is locked or disconnected."); return; }
    swprintf(command, sizeof(command) / sizeof(*command), L"cmd.exe /c \"\"%ls\"\"", cfg.launcher);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, cfg.root, &startup, &process)) {
        snprintf(reply, capacity, "Could not start the launcher (error %lu).", GetLastError()); return;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    snprintf(reply, capacity, "Started. It begins after a 10-second countdown; a work session then starts the "
        "Loggy3 recording and works until today's target. Send /status in a minute.");
}
static void handle_command(const char *chat, const char *text) {
    char command[32], reply[4000];
    size_t i;
    for (i = 0; text[i] && text[i] != ' ' && text[i] != '@' && i + 1 < sizeof(command); ++i)
        command[i] = (char)((text[i] >= 'A' && text[i] <= 'Z') ? text[i] + 32 : text[i]);
    command[i] = 0;
    if (!*cfg.chat) {
        snprintf(reply, sizeof(reply), "Setup: your chat id is %s\nPut it in notify.json as \"telegram_chat_id\": \"%s\", "
            "then restart the monitor.", chat, chat);
        send_text_to(chat, reply); note("setup: replied with chat id %s", chat); return;
    }
    if (strcmp(chat, cfg.chat)) { note("ignored a message from another chat (%s)", chat); return; }
    note("command %s", command);
    if (!strcmp(command, "/status")) { status_text(reply, sizeof(reply), 0); send_text(reply); }
    else if (!strcmp(command, "/screenshot")) { status_text(reply, sizeof(reply), 0); send_photo(reply); }
    else if (!strcmp(command, "/log")) { status_text(reply, sizeof(reply), 1); send_text(reply); }
    else if (!strcmp(command, "/pause") || !strcmp(command, "/resume") || !strcmp(command, "/stop")) {
        const wchar_t *name = command[1] == 'p' ? PLAYBACK_REMOTE_PAUSE_EVENT :
            command[1] == 'r' ? PLAYBACK_REMOTE_RESUME_EVENT : PLAYBACK_REMOTE_STOP_EVENT;
        if (!tutorial_running()) send_text("Playback is not running.");
        else if (!signal_playback(name)) send_text("Playback is running but did not accept remote commands (restart it with the new build).");
        else send_text(command[1] == 'p' ? "Pause sent. Playback pauses at its next safe point; /resume continues." :
            command[1] == 'r' ? "Resume sent." : "Stop sent. A work session finishes its line, saves, runs its ending and stops the "
            "Loggy3 recording; other playback releases input and stops at once. /start starts it again.");
    } else if (!strcmp(command, "/start") || !strcmp(command, "/run")) { run_launcher(reply, sizeof(reply)); send_text(reply); }
    else send_text("Commands:\n/status - progress and state\n/screenshot - picture of the desktop\n/log - recent log lines\n"
        "/start - start the day (the launcher in notify.json: the work session)\n"
        "/pause, /resume - like F8 (Loggy3 keeps recording)\n/stop - end the day: finish the line, save, stop recording");
}
/* Pulls the next "key":"value" string or number after a position in Telegram's compact JSON. */
static const char *json_text_after(const char *at, const char *key, const char *limit, char *out, size_t capacity) {
    const char *found = strstr(at, key);
    size_t used = 0;
    if (!found || (limit && found >= limit)) return NULL;
    found += strlen(key);
    if (*found == '"') {
        for (++found; *found && *found != '"' && used + 1 < capacity; ++found) {
            if (*found == '\\' && found[1]) ++found;
            out[used++] = *found;
        }
    } else while ((*found == '-' || (*found >= '0' && *found <= '9')) && used + 1 < capacity) out[used++] = *found++;
    out[used] = 0;
    return used ? found : NULL;
}
static long long handle_updates(const char *response, long long offset, int execute) {
    const char *at = response;
    while ((at = strstr(at, "\"update_id\":"))) {
        const char *next = strstr(at + 12, "\"update_id\":");
        char number[32], chat[32], text[256];
        long long id;
        json_text_after(at, "\"update_id\":", next, number, sizeof(number));
        id = _atoi64(number);
        if (id >= offset) offset = id + 1;
        if (execute && json_text_after(at, "\"chat\":{\"id\":", next, chat, sizeof(chat)) &&
            json_text_after(at, "\"text\":", next, text, sizeof(text)) && text[0] == '/') handle_command(chat, text);
        at += 12;
    }
    return offset;
}
static DWORD WINAPI command_thread(LPVOID unused) {
    long long offset = 0;
    char *response = NULL;
    (void)unused;
    /* Skip messages sent while the monitor was off, so an old /stop is never replayed. */
    if (telegram(L"getUpdates?offset=-1&timeout=0", NULL, NULL, 0, &response, 30000)) {
        offset = handle_updates(response, 0, 0); free(response);
    }
    for (;;) {
        wchar_t method[96];
        swprintf(method, 96, L"getUpdates?timeout=25&offset=%lld", offset);
        if (telegram(method, NULL, NULL, 0, &response, 40000)) {
            offset = handle_updates(response, offset, 1); free(response);
        } else Sleep(10000);
    }
    return 0;
}

/* Stop alert: written to monitor.log, and a screenshot is saved as logs\stop-DATE-TIME.jpg and sent
   with the reason as its caption, followed by the status and recent log lines. */
static void stop_alert(const char *headline) {
    SYSTEMTIME now;
    wchar_t shot[MAX_PATH];
    char reason[800], caption[1000], text[4000], alert[4600];
    GetLocalTime(&now);
    swprintf(shot, MAX_PATH, L"%ls\\stop-%04u%02u%02u-%02u%02u%02u.jpg", cfg.logs, now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond);
    EnterCriticalSection(&state_lock);
    snprintf(reason, sizeof(reason), "%s", *state.reason ? state.reason : "nothing in the playback log explains it");
    LeaveCriticalSection(&state_lock);
    note("%s Reason: %s", headline, reason);
    snprintf(caption, sizeof(caption), "%s\n\nWhy:\n%s", headline, reason);
    send_photo_saved(caption, shot);
    if (!*cfg.chat) return;
    status_text(text, sizeof(text), 1);
    snprintf(alert, sizeof(alert), "%s\n\n%s", headline, text);
    send_text(alert);
}

int main(void) {
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc), log_path[MAX_PATH];
    const wchar_t *config_path = L"notify.json";
    HANDLE single;
    ULONGLONG last_heartbeat, stall_alerted = 0;
    int was_running, was_desktop, stop_reported = 0;
    char events[8][200];
    unsigned event_count, i;
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) SetProcessDPIAware();
    if (argv && argc == 3 && !wcscmp(argv[1], L"--screenshot")) {
        /* Local check of screen capture; nothing is sent. */
        unsigned char *jpeg; DWORD size; FILE *out;
        if (!capture_jpeg(&jpeg, &size) || !(out = _wfopen(argv[2], L"wb"))) { puts("Screenshot failed."); return 1; }
        fwrite(jpeg, 1, size, out); fclose(out); free(jpeg);
        printf("Saved %lu-byte JPEG.\n", size); return 0;
    }
    if (argv && argc >= 2 && argc <= 3 && !wcscmp(argv[1], L"--status")) {
        /* Local check of the status text (what /status and screenshot captions say); nothing is sent. */
        char text[4000];
        InitializeCriticalSection(&state_lock);
        if (load_config(argc == 3 ? argv[2] : L"notify.json")) {
            EnterCriticalSection(&state_lock); poll_log(1, events, &event_count); LeaveCriticalSection(&state_lock);
        }
        status_text(text, sizeof(text), 0);
        puts(text);
        return 0;
    }
    if (argv && argc == 3 && !wcscmp(argv[1], L"--config")) config_path = argv[2];
    else if (argv && argc != 1) { puts("monitor [--config notify.json] | monitor --status [notify.json] | monitor --screenshot FILE.jpg"); return 2; }
    single = CreateMutexW(NULL, FALSE, L"Local\\TutorialAutomationMonitor");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { puts("The monitor is already running."); return 0; }
    if (!load_config(config_path)) return 2;
    CreateDirectoryW(cfg.logs, NULL);
    swprintf(log_path, MAX_PATH, L"%ls\\monitor.log", cfg.logs);
    own_log = _wfopen(log_path, L"ab");
    InitializeCriticalSection(&state_lock); InitializeCriticalSection(&send_lock);
    if (!(http = WinHttpOpen(L"TutorialMonitor/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0))) { fputs("WinHTTP is unavailable\n", stderr); return 2; }
    /* Keep the machine and display awake so the session stays visible for RustDesk and screenshots. */
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    EnterCriticalSection(&state_lock); poll_log(1, events, &event_count); LeaveCriticalSection(&state_lock);
    if (!*cfg.chat) {
        puts("Setup mode: send any message to your bot in Telegram; it replies with your chat id.");
        note("monitor started in setup mode");
    } else {
        char text[4000];
        status_text(text, sizeof(text), 0);
        { char start[4200]; snprintf(start, sizeof(start), "Monitor started.\n\n%s", text);
          if (!send_text(start)) puts("Could not reach Telegram; check the token and network. Retrying in the background."); }
        note("monitor started; heartbeat %u min, stall alert %u min", cfg.heartbeat_minutes, cfg.stall_minutes);
    }
    CloseHandle(CreateThread(NULL, 0, command_thread, NULL, 0, NULL));
    last_heartbeat = GetTickCount64();
    was_running = tutorial_running(); was_desktop = window_desktop_ready();
    for (;;) {
        int kind, running, desktop, paused, finished;
        ULONGLONG idle_ms;
        char text[4000];
        Sleep(3000);
        EnterCriticalSection(&state_lock);
        kind = poll_log(0, events, &event_count);
        paused = state.paused; finished = state.finished;
        idle_ms = state.last_change ? GetTickCount64() - state.last_change : 0;
        LeaveCriticalSection(&state_lock);
        if (!*cfg.chat) continue;
        for (i = 0; i < event_count; ++i) send_text(events[i]);
        running = tutorial_running(); desktop = window_desktop_ready();
        /* stop_reported: 1 after a stop that an automatic restart may still follow, 2 once the end
           of the run was reported. The exit after a stop is reported only if nothing said the run ended. */
        if (!was_running && running) stop_reported = 0;
        if (kind == 3) {
            stop_reported = 2;
            stop_alert(was_running ? "Playback ENDED: the automatic restart failed. Fix the cause, then send /start."
                                   : "Playback did not start: the startup check failed. Fix the cause, then send /start.");
        } else if (kind == 2) {
            stop_reported = running ? 1 : 2;
            stop_alert(running ? "Playback STOPPED. An automatic restart may follow."
                               : "Playback STOPPED and tutorial.exe has exited.");
        } else if (was_running && !running && !finished && stop_reported != 2) {
            Sleep(4000);
            EnterCriticalSection(&state_lock);
            poll_log(1, events, &event_count);
            if (!stop_reported) state.reason[0] = 0;
            LeaveCriticalSection(&state_lock);
            stop_reported = 2;
            stop_alert("tutorial.exe is no longer running. Send /start to resume.");
        }
        if (was_desktop && !desktop) send_text("ALERT: the desktop became locked or disconnected. Playback cannot continue until it is unlocked.");
        else if (!was_desktop && desktop) send_text("The desktop is unlocked again.");
        if (running && !paused && idle_ms > (ULONGLONG)cfg.stall_minutes * 60000) {
            if (!stall_alerted) {
                char alert[4200];
                status_text(text, sizeof(text), 1);
                snprintf(alert, sizeof(alert), "Possible stall: no log activity for %u+ minutes.\n\n%s", cfg.stall_minutes, text);
                send_photo(alert);
                stall_alerted = 1;
            }
        } else stall_alerted = 0;
        if (cfg.heartbeat_minutes && GetTickCount64() - last_heartbeat >= (ULONGLONG)cfg.heartbeat_minutes * 60000) {
            char beat[4200];
            last_heartbeat = GetTickCount64();
            status_text(text, sizeof(text), 0);
            snprintf(beat, sizeof(beat), "Scheduled update (every %u min)\n\n%s", cfg.heartbeat_minutes, text);
            send_photo(beat);
        }
        was_running = running; was_desktop = desktop;
    }
    (void)single;
    return 0;
}
