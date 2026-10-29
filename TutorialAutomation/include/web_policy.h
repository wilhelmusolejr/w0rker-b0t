#ifndef WEB_POLICY_H
#define WEB_POLICY_H
#include <stddef.h>
#include <wchar.h>
/* Which links the browser session may click. Ported from browser_explorer.py:
   stay on the starting site, and skip sign-in, account-changing, and download links. */
int web_host(const wchar_t *url, wchar_t *host, size_t capacity);
/* Same host, or the host's plain/www counterpart. */
int web_same_site(const wchar_t *site_url, const wchar_t *url);
int web_url_allowed(const wchar_t *url);
/* Link or button text that would change an account, buy, download, or submit. */
int web_name_safe(const wchar_t *name);
/* Compare without fragment and trailing slash. */
int web_same_page(const wchar_t *a, const wchar_t *b);
#endif
