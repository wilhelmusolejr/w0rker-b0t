#include "saved_text_wait.h"
#include <stdlib.h>
#include <string.h>
SavedTextStatus saved_text_wait(const wchar_t *expected, const size_t *required,
                                const SavedTextIO *io, unsigned timeout_ms, SavedTextResult *result) {
    uint64_t started;
    if (!result) return SAVED_TEXT_INVALID;
    memset(result,0,sizeof(*result));
    if (!expected || !io || !io->read || !io->poll || !io->wait || !io->now ||
        (required && *required > wcslen(expected))) return SAVED_TEXT_INVALID;
    started = io->now(io->context);
    for (;;) {
        wchar_t *snapshot = NULL;
        uint64_t elapsed;
        if (!io->poll(io->context)) return SAVED_TEXT_CANCELLED;
        if (io->read(io->context,&snapshot) && snapshot) {
            size_t i = 0;
            while (snapshot[i] && expected[i] && snapshot[i] == expected[i]) ++i;
            result->length = wcslen(snapshot);
            if (snapshot[i]) {
                result->mismatch = i; result->expected_character = expected[i]; result->saved_character = snapshot[i];
                free(snapshot); return SAVED_TEXT_DIFFERENT;
            }
            free(snapshot);
            if (!required || result->length == *required) return SAVED_TEXT_READY;
            /* A newer, longer prefix also means the editor contains unexpected input. */
            if (result->length > *required) return SAVED_TEXT_DIFFERENT;
        } else free(snapshot);
        elapsed = io->now(io->context)-started;
        if (elapsed >= timeout_ms) return SAVED_TEXT_TIMEOUT;
        if (!io->wait(io->context,(unsigned)(timeout_ms-elapsed > 100 ? 100 : timeout_ms-elapsed)))
            return SAVED_TEXT_CANCELLED;
    }
}
