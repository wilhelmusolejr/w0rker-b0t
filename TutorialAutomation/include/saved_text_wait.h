#ifndef SAVED_TEXT_WAIT_H
#define SAVED_TEXT_WAIT_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
typedef struct {
    void *context;
    /* A successful read transfers a malloc-allocated, LF-normalized snapshot. */
    int (*read)(void *context, wchar_t **snapshot);
    int (*poll)(void *context);
    int (*wait)(void *context, unsigned milliseconds);
    /* Active time: excludes explicit pauses. */
    uint64_t (*now)(void *context);
} SavedTextIO;
typedef enum {
    SAVED_TEXT_READY, SAVED_TEXT_DIFFERENT, SAVED_TEXT_TIMEOUT,
    SAVED_TEXT_CANCELLED, SAVED_TEXT_INVALID
} SavedTextStatus;
typedef struct {
    size_t length, mismatch;
    wchar_t expected_character, saved_character;
} SavedTextResult;
SavedTextStatus saved_text_wait(const wchar_t *expected, const size_t *required_length,
                                const SavedTextIO *io, unsigned timeout_ms, SavedTextResult *result);
#endif
