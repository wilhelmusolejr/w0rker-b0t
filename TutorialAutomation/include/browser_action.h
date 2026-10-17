#ifndef BROWSER_ACTION_H
#define BROWSER_ACTION_H
#include "playback.h"
int browser_reading_visit(Playback *p, const wchar_t *url, DWORD duration_ms,
                          uint32_t seed, HWND *browser_window);
int browser_checkpoint_due(const Playback *p, unsigned completed_lines, int more_code);
int browser_checkpoint(Playback *p, unsigned completed_lines);
int browser_docs_visit(Playback *p, const wchar_t *query, const wchar_t *url,
                       DWORD duration_ms, uint32_t seed, HWND *browser_window);
/* A nonempty query searches Google first; NULL opens the supplied page directly. */
int browser_topic_visit(Playback *p, const wchar_t *query, const wchar_t *url,
                        DWORD duration_ms, uint32_t seed, HWND *browser_window);
/* Search Google for the query, then open and read 1-3 visible organic (non-sponsored) results. */
int browser_research_visit(Playback *p, const wchar_t *query, DWORD duration_ms,
                           uint32_t seed, HWND *browser_window, unsigned results);
#endif
