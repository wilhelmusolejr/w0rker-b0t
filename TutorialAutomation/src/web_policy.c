#include "web_policy.h"
#include "browser_scroll_plan.h"
#include <string.h>
#include <wctype.h>

static const wchar_t *const blocked_segments[] = {
    L"login", L"logout", L"signin", L"signout", L"signup", L"sign-in", L"sign-out", L"log-in", L"log-out",
    L"register", L"checkout", L"purchase", L"subscribe", L"unsubscribe", L"delete", L"remove", L"edit",
    L"upload", L"create", L"settings", L"preferences", L"follow", L"unfollow", L"star", L"unstar", L"new",
    L"account", L"cart", L"download", L"downloads", L"auth", L"oauth"
};
static const wchar_t *const download_suffixes[] = {
    L".zip", L".exe", L".msi", L".dmg", L".pkg", L".deb", L".rpm", L".tar", L".gz", L".bz2", L".xz",
    L".7z", L".rar", L".iso", L".pdf", L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx", L".apk"
};
static const wchar_t *const unsafe_words[] = {
    L"follow", L"unfollow", L"star", L"unstar", L"watch", L"unwatch", L"subscribe", L"unsubscribe", L"sponsor",
    L"delete", L"remove", L"report", L"block", L"submit", L"save", L"create", L"upload", L"download",
    L"sign in", L"sign up", L"sign out", L"log in", L"log out", L"login", L"logout", L"purchase", L"buy",
    L"merge", L"revert", L"fork", L"checkout", L"print", L"add to cart", L"donate", L"install", L"accept",
    L"register"
};

int web_host(const wchar_t *url, wchar_t *host, size_t cap) {
    const wchar_t *start = wcsstr(url, L"://");
    size_t n = 0;
    if (!start || !cap) return 0;
    start += 3;
    /* Reject embedded credentials such as user@host. */
    { const wchar_t *at = start;
      while (*at && *at != L'/' && *at != L'?' && *at != L'#') { if (*at == L'@') return 0; ++at; } }
    while (start[n] && start[n] != L'/' && start[n] != L'?' && start[n] != L'#' && start[n] != L':') {
        if (n + 1 >= cap) return 0;
        host[n] = (wchar_t)towlower(start[n]); ++n;
    }
    while (n && host[n-1] == L'.') --n;
    host[n] = 0;
    return n > 0;
}
int web_same_site(const wchar_t *site_url, const wchar_t *url) {
    wchar_t a[256], b[256];
    const wchar_t *pa, *pb;
    if (!web_host(site_url, a, 256) || !web_host(url, b, 256)) return 0;
    pa = wcsncmp(a, L"www.", 4) ? a : a + 4;
    pb = wcsncmp(b, L"www.", 4) ? b : b + 4;
    return !wcscmp(pa, pb);
}
static int path_blocked(const wchar_t *path, size_t length) {
    size_t i = 0;
    while (i < length) {
        size_t start, end, k;
        while (i < length && path[i] == L'/') ++i;
        start = i;
        while (i < length && path[i] != L'/') ++i;
        end = i;
        if (end - start == 2 && path[start] == L'.' && path[start+1] == L'.') return 1;
        for (k = 0; k < sizeof(blocked_segments)/sizeof(*blocked_segments); ++k)
            if (wcslen(blocked_segments[k]) == end - start && !_wcsnicmp(path + start, blocked_segments[k], end - start)) return 1;
    }
    for (i = 0; i < sizeof(download_suffixes)/sizeof(*download_suffixes); ++i) {
        size_t s = wcslen(download_suffixes[i]);
        if (length >= s && !_wcsnicmp(path + length - s, download_suffixes[i], s)) return 1;
    }
    return 0;
}
int web_url_allowed(const wchar_t *url) {
    wchar_t host[256];
    const wchar_t *path, *query;
    size_t length;
    if (!url || !browser_url_valid(url) || !web_host(url, host, 256)) return 0;
    path = wcschr(wcsstr(url, L"://") + 3, L'/');
    if (!path) return 1;
    length = wcscspn(path, L"?#");
    /* Percent-encoded paths could hide a blocked segment; treat them as unknown. */
    if (wcsstr(path, L"%2F") || wcsstr(path, L"%2f") || wcsstr(path, L"%2E") || wcsstr(path, L"%2e") || path_blocked(path, length)) return 0;
    query = path[length] == L'?' ? path + length + 1 : NULL;
    if (query) {
        static const wchar_t *const keys[] = {L"delete=", L"download=", L"filename=", L"logout", L"action=delete",
            L"action=remove", L"action=edit", L"action=logout", L"do=delete"};
        size_t k;
        for (k = 0; k < sizeof(keys)/sizeof(*keys); ++k) {
            const wchar_t *hit = query;
            size_t n = wcslen(keys[k]);
            for (; *hit && *hit != L'#'; ++hit)
                if ((hit == query || hit[-1] == L'&') && !_wcsnicmp(hit, keys[k], n)) return 0;
        }
    }
    return 1;
}
int web_name_safe(const wchar_t *name) {
    size_t i, n = wcslen(name);
    for (i = 0; i < sizeof(unsafe_words)/sizeof(*unsafe_words); ++i) {
        size_t w = wcslen(unsafe_words[i]), at;
        for (at = 0; at + w <= n; ++at) {
            int before = at == 0 || !iswalnum(name[at-1]), after = at + w == n || !iswalnum(name[at+w]);
            if (before && after && !_wcsnicmp(name + at, unsafe_words[i], w)) return 0;
        }
    }
    return 1;
}
int web_same_page(const wchar_t *a, const wchar_t *b) {
    size_t la = wcscspn(a, L"#"), lb = wcscspn(b, L"#");
    while (la && a[la-1] == L'/') --la;
    while (lb && b[lb-1] == L'/') --lb;
    return la == lb && !_wcsnicmp(a, b, la);
}
